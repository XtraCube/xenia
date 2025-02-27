/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2021 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/ui/windowed_app_context_android.h"

#include <android/asset_manager_jni.h>
#include <android/configuration.h>
#include <android/log.h>
#include <android/looper.h>
#include <fcntl.h>
#include <jni.h>
#include <unistd.h>
#include <array>
#include <cstdint>

#include "xenia/base/assert.h"
#include "xenia/base/logging.h"
#include "xenia/base/main_android.h"
#include "xenia/ui/windowed_app.h"

namespace xe {
namespace ui {

void AndroidWindowedAppContext::NotifyUILoopOfPendingFunctions() {
  // Don't check ui_thread_looper_callback_registered_, as it's owned
  // exclusively by the UI thread, while this may be called by any, and in case
  // of a pipe error, the callback will be invoked by the looper, which will
  // trigger all the necessary shutdown, and the pending functions will be
  // called anyway by the shutdown.
  UIThreadLooperCallbackCommand command =
      UIThreadLooperCallbackCommand::kExecutePendingFunctions;
  if (write(ui_thread_looper_callback_pipe_[1], &command, sizeof(command)) !=
      sizeof(command)) {
    XELOGE(
        "AndroidWindowedAppContext: Failed to write a pending function "
        "execution command to the UI thread looper callback pipe");
    return;
  }
  ALooper_wake(ui_thread_looper_);
}

void AndroidWindowedAppContext::PlatformQuitFromUIThread() {
  // All the shutdown will be done in onDestroy of the activity.
  if (activity_ && activity_method_finish_) {
    ui_thread_jni_env_->CallVoidMethod(activity_, activity_method_finish_);
  }
}

AndroidWindowedAppContext*
AndroidWindowedAppContext::JniActivityInitializeWindowedAppOnCreate(
    JNIEnv* jni_env, jobject activity, jstring windowed_app_identifier,
    jobject asset_manager) {
  WindowedApp::Creator app_creator;
  {
    const char* windowed_app_identifier_c_str =
        jni_env->GetStringUTFChars(windowed_app_identifier, nullptr);
    if (!windowed_app_identifier_c_str) {
      __android_log_write(
          ANDROID_LOG_ERROR, "AndroidWindowedAppContext",
          "Failed to get the UTF-8 string for the windowed app identifier");
      return nullptr;
    }
    app_creator = WindowedApp::GetCreator(windowed_app_identifier_c_str);
    if (!app_creator) {
      __android_log_print(ANDROID_LOG_ERROR, "AndroidWindowedAppContext",
                          "Failed to get the creator for the windowed app %s",
                          windowed_app_identifier_c_str);
      jni_env->ReleaseStringUTFChars(windowed_app_identifier,
                                     windowed_app_identifier_c_str);
      return nullptr;
    }
    jni_env->ReleaseStringUTFChars(windowed_app_identifier,
                                   windowed_app_identifier_c_str);
  }

  AndroidWindowedAppContext* app_context = new AndroidWindowedAppContext;
  if (!app_context->Initialize(jni_env, activity, asset_manager)) {
    delete app_context;
    return nullptr;
  }

  if (!app_context->InitializeApp(app_creator)) {
    // InitializeApp might have sent commands to the UI thread looper callback
    // pipe, perform deferred destruction.
    app_context->RequestDestruction();
    return nullptr;
  }

  return app_context;
}

void AndroidWindowedAppContext::JniActivityOnDestroy() {
  if (app_) {
    app_->InvokeOnDestroy();
    app_.reset();
  }
  RequestDestruction();
}

AndroidWindowedAppContext::~AndroidWindowedAppContext() { Shutdown(); }

bool AndroidWindowedAppContext::Initialize(JNIEnv* ui_thread_jni_env,
                                           jobject activity,
                                           jobject asset_manager) {
  // Xenia logging is not initialized yet - use __android_log_write or
  // __android_log_print until InitializeAndroidAppFromMainThread is done.

  ui_thread_jni_env_ = ui_thread_jni_env;

  // Initialize the asset manager for retrieving the current configuration.
  asset_manager_jobject_ = ui_thread_jni_env_->NewGlobalRef(asset_manager);
  if (!asset_manager_jobject_) {
    __android_log_write(
        ANDROID_LOG_ERROR, "AndroidWindowedAppContext",
        "Failed to create a global reference to the asset manager");
    Shutdown();
    return false;
  }
  asset_manager_ =
      AAssetManager_fromJava(ui_thread_jni_env_, asset_manager_jobject_);
  if (!asset_manager_) {
    __android_log_write(ANDROID_LOG_ERROR, "AndroidWindowedAppContext",
                        "Failed to create get the AAssetManager");
    Shutdown();
    return false;
  }

  // Get the initial configuration.
  configuration_ = AConfiguration_new();
  if (!configuration_) {
    __android_log_write(ANDROID_LOG_ERROR, "AndroidWindowedAppContext",
                        "Failed to create an AConfiguration");
    Shutdown();
    return false;
  }
  AConfiguration_fromAssetManager(configuration_, asset_manager_);

  // Initialize Xenia globals that may depend on the API level, as well as
  // logging.
  xe::InitializeAndroidAppFromMainThread(
      AConfiguration_getSdkVersion(configuration_));
  android_base_initialized_ = true;

  // Initialize interfacing with the WindowedAppActivity.
  activity_ = ui_thread_jni_env_->NewGlobalRef(activity);
  if (!activity_) {
    XELOGE(
        "AndroidWindowedAppContext: Failed to create a global reference to the "
        "activity");
    Shutdown();
    return false;
  }
  {
    jclass activity_class_local_ref =
        ui_thread_jni_env_->GetObjectClass(activity);
    if (!activity_class_local_ref) {
      XELOGE("AndroidWindowedAppContext: Failed to get the activity class");
      Shutdown();
      return false;
    }
    activity_class_ = reinterpret_cast<jclass>(ui_thread_jni_env_->NewGlobalRef(
        reinterpret_cast<jobject>(activity_class_local_ref)));
    ui_thread_jni_env_->DeleteLocalRef(
        reinterpret_cast<jobject>(activity_class_local_ref));
  }
  if (!activity_class_) {
    XELOGE(
        "AndroidWindowedAppContext: Failed to create a global reference to the "
        "activity class");
    Shutdown();
    return false;
  }
  bool activity_ids_obtained = true;
  activity_ids_obtained &=
      (activity_method_finish_ = ui_thread_jni_env_->GetMethodID(
           activity_class_, "finish", "()V")) != nullptr;
  if (!activity_ids_obtained) {
    XELOGE("AndroidWindowedAppContext: Failed to get the activity class IDs");
    Shutdown();
    return false;
  }

  // Initialize sending commands to the UI thread looper callback, for
  // requesting function calls in the UI thread.
  ui_thread_looper_ = ALooper_forThread();
  // The context may be created only in the UI thread, which must have an
  // internal looper.
  assert_not_null(ui_thread_looper_);
  if (!ui_thread_looper_) {
    XELOGE("AndroidWindowedAppContext: Failed to get the UI thread looper");
    Shutdown();
    return false;
  }
  // The looper can be woken up by other threads, so acquiring it. Shutdown
  // assumes that if ui_thread_looper_ is not null, it has been acquired.
  ALooper_acquire(ui_thread_looper_);
  if (pipe(ui_thread_looper_callback_pipe_.data())) {
    XELOGE(
        "AndroidWindowedAppContext: Failed to create the UI thread looper "
        "callback pipe");
    Shutdown();
    return false;
  }
  if (ALooper_addFd(ui_thread_looper_, ui_thread_looper_callback_pipe_[0],
                    ALOOPER_POLL_CALLBACK, ALOOPER_EVENT_INPUT,
                    UIThreadLooperCallback, this) != 1) {
    XELOGE(
        "AndroidWindowedAppContext: Failed to add the callback to the UI "
        "thread looper");
    Shutdown();
    return false;
  }
  ui_thread_looper_callback_registered_ = true;

  return true;
}

void AndroidWindowedAppContext::Shutdown() {
  if (app_) {
    app_->InvokeOnDestroy();
    app_.reset();
  }

  // The app should destroy the window, but make sure everything is cleaned up
  // anyway.
  assert_null(activity_window_);
  activity_window_ = nullptr;

  if (ui_thread_looper_callback_registered_) {
    ALooper_removeFd(ui_thread_looper_, ui_thread_looper_callback_pipe_[0]);
    ui_thread_looper_callback_registered_ = false;
  }
  for (int& pipe_fd : ui_thread_looper_callback_pipe_) {
    if (pipe_fd == -1) {
      continue;
    }
    close(pipe_fd);
    pipe_fd = -1;
  }
  if (ui_thread_looper_) {
    ALooper_release(ui_thread_looper_);
    ui_thread_looper_ = nullptr;
  }

  activity_method_finish_ = nullptr;
  if (activity_class_) {
    ui_thread_jni_env_->DeleteGlobalRef(
        reinterpret_cast<jobject>(activity_class_));
    activity_class_ = nullptr;
  }
  if (activity_) {
    ui_thread_jni_env_->DeleteGlobalRef(activity_);
    activity_ = nullptr;
  }

  if (android_base_initialized_) {
    xe::ShutdownAndroidAppFromMainThread();
    android_base_initialized_ = false;
  }

  if (configuration_) {
    AConfiguration_delete(configuration_);
    configuration_ = nullptr;
  }

  asset_manager_ = nullptr;
  if (asset_manager_jobject_) {
    ui_thread_jni_env_->DeleteGlobalRef(asset_manager_jobject_);
    asset_manager_jobject_ = nullptr;
  }

  ui_thread_jni_env_ = nullptr;
}

void AndroidWindowedAppContext::RequestDestruction() {
  // According to ALooper_removeFd documentation:
  // "...it is possible for the callback to already be running or for it to run
  //  one last time if the file descriptor was already signalled. Calling code
  //  is responsible for ensuring that this case is safely handled. For example,
  //  if the callback takes care of removing itself during its own execution
  //  either by returning 0 or by calling this method..."
  // If the looper callback is registered, the pipe may have pending commands,
  // and thus the callback may still be called with the pointer to the context
  // as the user data.
  if (!ui_thread_looper_callback_registered_) {
    delete this;
    return;
  }
  UIThreadLooperCallbackCommand command =
      UIThreadLooperCallbackCommand::kDestroy;
  if (write(ui_thread_looper_callback_pipe_[1], &command, sizeof(command)) !=
      sizeof(command)) {
    XELOGE(
        "AndroidWindowedAppContext: Failed to write a destruction command to "
        "the UI thread looper callback pipe");
    delete this;
    return;
  }
  ALooper_wake(ui_thread_looper_);
}

int AndroidWindowedAppContext::UIThreadLooperCallback(int fd, int events,
                                                      void* data) {
  // In case of errors, destruction of the pipe (most importantly the write end)
  // must not be done here immediately as other threads, which may still be
  // sending commands, would not be aware of that.
  auto app_context = static_cast<AndroidWindowedAppContext*>(data);
  if (events &
      (ALOOPER_EVENT_ERROR | ALOOPER_EVENT_HANGUP | ALOOPER_EVENT_INVALID)) {
    // Will return 0 to unregister self, this file descriptor is not usable
    // anymore, so let everything potentially referencing it in QuitFromUIThread
    // know.
    app_context->ui_thread_looper_callback_registered_ = false;
    XELOGE(
        "AndroidWindowedAppContext: The UI thread looper callback pipe file "
        "descriptor has encountered an error condition during polling");
    app_context->QuitFromUIThread();
    return 0;
  }
  if (!(events & ALOOPER_EVENT_INPUT)) {
    // Spurious callback call. Need a non-empty pipe.
    return 1;
  }
  // Process one command with a blocking `read`. The callback will be invoked
  // again and again if there is still data after this read.
  UIThreadLooperCallbackCommand command;
  switch (read(fd, &command, sizeof(command))) {
    case sizeof(command):
      break;
    case -1:
      // Will return 0 to unregister self, this file descriptor is not usable
      // anymore, so let everything potentially referencing it in
      // QuitFromUIThread know.
      app_context->ui_thread_looper_callback_registered_ = false;
      XELOGE(
          "AndroidWindowedAppContext: The UI thread looper callback pipe file "
          "descriptor has encountered an error condition during reading");
      app_context->QuitFromUIThread();
      return 0;
    default:
      // Something like incomplete data - shouldn't be happening, but not a
      // reported error.
      return 1;
  }
  switch (command) {
    case UIThreadLooperCallbackCommand::kDestroy:
      // Final destruction requested. Will unregister self by returning 0, so
      // set ui_thread_looper_callback_registered_ to false so Shutdown won't
      // try to unregister it too.
      app_context->ui_thread_looper_callback_registered_ = false;
      delete app_context;
      return 0;
    case UIThreadLooperCallbackCommand::kExecutePendingFunctions:
      app_context->ExecutePendingFunctionsFromUIThread();
      break;
  }
  return 1;
}

bool AndroidWindowedAppContext::InitializeApp(std::unique_ptr<WindowedApp> (
    *app_creator)(WindowedAppContext& app_context)) {
  assert_null(app_);
  app_ = app_creator(*this);
  if (!app_->OnInitialize()) {
    app_->InvokeOnDestroy();
    app_.reset();
    return false;
  }
  return true;
}

}  // namespace ui
}  // namespace xe

extern "C" {

JNIEXPORT jlong JNICALL
Java_jp_xenia_emulator_WindowedAppActivity_initializeWindowedAppOnCreateNative(
    JNIEnv* jni_env, jobject activity, jstring windowed_app_identifier,
    jobject asset_manager) {
  return reinterpret_cast<jlong>(
      xe::ui::AndroidWindowedAppContext ::
          JniActivityInitializeWindowedAppOnCreate(
              jni_env, activity, windowed_app_identifier, asset_manager));
}

JNIEXPORT void JNICALL
Java_jp_xenia_emulator_WindowedAppActivity_onDestroyNative(
    JNIEnv* jni_env, jobject activity, jlong app_context_ptr) {
  reinterpret_cast<xe::ui::AndroidWindowedAppContext*>(app_context_ptr)
      ->JniActivityOnDestroy();
}

}  // extern "C"
