# Copyright 2013 Ben Vanik. All Rights Reserved.
{
  'default_configuration': 'release',

  'variables': {
    'configurations': {
      'debug': {
      },
      'release': {
      },
    },

    'library%': 'static_library',
    'target_arch%': 'x64',
  },

  'target_defaults': {
    'include_dirs': [
      'include/',
    ],

    'defines': [
      '__STDC_LIMIT_MACROS=1',
      '__STDC_CONSTANT_MACROS=1',
      '_ISOC99_SOURCE=1',
    ],

    'conditions': [
      ['OS == "win"', {
        'defines': [
          '_WIN64=1',
          '_AMD64_=1',
        ],
      }],
    ],

    'cflags': [
      #'-std=c99',
    ],

    'configurations': {
      'common_base': {
        'abstract': 1,

        'msvs_configuration_platform': 'x64',
        'msvs_configuration_attributes': {
          'OutputDirectory': '$(SolutionDir)$(ConfigurationName)',
          'IntermediateDirectory': '$(OutDir)\\obj\\$(ProjectName)',
          'CharacterSet': '1',
        },
        'msvs_disabled_warnings': [],
        'msvs_configuration_platform': 'x64',
        'msvs_cygwin_shell': '0',
        'msvs_settings': {
          'VCCLCompilerTool': {
            #'MinimalRebuild': 'true',
            'BufferSecurityCheck': 'true',
            'EnableFunctionLevelLinking': 'true',
            'RuntimeTypeInfo': 'false',
            'WarningLevel': '3',
            #'WarnAsError': 'true',
            'DebugInformationFormat': '3',
            'ExceptionHandling': '1', # /EHsc
            'AdditionalOptions': [
              '/MP',    # Multiprocessor build
              '/TP',    # Compile as C++
              '/EHsc',  # C++ exception handling,
            ],
          },
          #'VCLibrarianTool': {
          #  'AdditionalLibraryDirectories!':
          #    ['<(DEPTH)/third_party/platformsdk_win7/files/Lib'],
          #  'AdditionalLibraryDirectories':
          #    ['<(DEPTH)/third_party/platformsdk_win7/files/Lib/x64'],
          #},
          'VCLinkerTool': {
            'GenerateDebugInformation': 'true',
            #'LinkIncremental': '1', # 1 = NO, 2 = YES
            'TargetMachine': '17', # x86 - 64
            'AdditionalLibraryDirectories': [
            ],
            #'AdditionalLibraryDirectories!':
            #  ['<(DEPTH)/third_party/platformsdk_win7/files/Lib'],
            #'AdditionalLibraryDirectories':
            #  ['<(DEPTH)/third_party/platformsdk_win7/files/Lib/x64'],
          },
        },

        'scons_settings': {
          'sconsbuild_dir': '<(DEPTH)/build/xenia/',
        },

        'xcode_settings': {
          'SYMROOT': '<(DEPTH)/build/xenia/',
          'ALWAYS_SEARCH_USER_PATHS': 'NO',
          'ARCHS': ['x86_64'],
          #'CLANG_CXX_LANGUAGE_STANDARD': 'c++0x',
          'COMBINE_HIDPI_IMAGES': 'YES',
          'GCC_C_LANGUAGE_STANDARD': 'gnu99',
          'GCC_SYMBOLS_PRIVATE_EXTERN': 'YES',
          #'GCC_TREAT_WARNINGS_AS_ERRORS': 'YES',
          'GCC_WARN_ABOUT_MISSING_NEWLINE': 'YES',
          'GCC_VERSION': 'com.apple.compilers.llvm.clang.1_0',
          'WARNING_CFLAGS': ['-Wall', '-Wendif-labels'],
          'LIBRARY_SEARCH_PATHS': [
          ],
        },

        'defines': [
        ],
      },

      'debug': {
        'inherit_from': ['common_base',],
        'defines': [
          'DEBUG',
        ],
        'msvs_configuration_attributes': {
          'OutputDirectory': '<(DEPTH)\\build\\xenia\\debug',
        },
        'msvs_settings': {
          'VCCLCompilerTool': {
            'Optimization': '0',
            'BasicRuntimeChecks': '0',  # disable /RTC1 when compiling /O2
            'DebugInformationFormat': '3',
            'ExceptionHandling': '0',
            'RuntimeTypeInfo': 'false',
            'OmitFramePointers': 'false',
          },
          'VCLinkerTool': {
            'LinkIncremental': '2',
            'GenerateDebugInformation': 'true',
            'StackReserveSize': '2097152',
          },
        },
        'xcode_settings': {
          'GCC_OPTIMIZATION_LEVEL': '0',
        },
      },
      'debug_x64': {
        'inherit_from': ['debug',],
      },

      'release': {
        'inherit_from': ['common_base',],
        'defines': [
          'RELEASE',
          'NDEBUG',
        ],
        'msvs_configuration_attributes': {
          'OutputDirectory': '<(DEPTH)\\build\\xenia\\release',
        },
        'msvs_settings': {
          'VCCLCompilerTool': {
            'Optimization': '2',
            'InlineFunctionExpansion': '2',
            'EnableIntrinsicFunctions': 'true',
            'FavorSizeOrSpeed': '0',
            'ExceptionHandling': '0',
            'RuntimeTypeInfo': 'false',
            'OmitFramePointers': 'false',
            'StringPooling': 'true',
          },
          'VCLinkerTool': {
            'LinkIncremental': '1',
            'GenerateDebugInformation': 'true',
            'OptimizeReferences': '2',
            'EnableCOMDATFolding': '2',
            'StackReserveSize': '2097152',
          },
        },
      },
      'release_x64': {
        'inherit_from': ['release',],
      },
    },
  },
}
