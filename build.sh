#########################################################################
# File Name: build.sh
# Author: billowqiu
# mail: billowqiu@163.com
# Created Time: 2023-12-26 11:06:08
# Last Changed: 2023-12-26 11:21:34
#########################################################################
#!/bin/bash
CC=/usr/local/bin/clang CXX=/usr/local/bin/clang++  bazel --output_user_root=/data1/cache/bazel build -s --verbose_failures -s --copt="-DENVOY_IGNORE_GLIBCXX_USE_CXX11_ABI_ERROR=1" -c opt //:envoy
CC=/usr/local/bin/clang CXX=/usr/local/bin/clang++  bazel --output_user_root=/data1/cache/bazel build -s -verbose_failures -s --copt="-DENVOY_IGNORE_GLIBCXX_USE_CXX11_ABI_ERROR=1" -c opt //http-filter-example:envoy
CC=/usr/local/bin/clang CXX=/usr/local/bin/clang++  bazel --output_user_root=/data1/cache/bazel build -s --verbose_failures -s --copt="-DENVOY_IGNORE_GLIBCXX_USE_CXX11_ABI_ERROR=1" -c opt //nacos2-filter-example:envoy
