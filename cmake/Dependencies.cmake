CPMAddPackage(
    NAME libhv
    URL https://github.com/ithewei/libhv/archive/refs/tags/v1.3.4.tar.gz
    URL_HASH SHA256=f0a9a197f90da55cc3ff104f9c7a27cc927f117b6c18613c3292726068588e10
    OPTIONS
        "BUILD_SHARED OFF"
        "BUILD_STATIC ON"
        "BUILD_EXAMPLES OFF"
        "BUILD_UNITTEST OFF"
)

CPMAddPackage(
    NAME cmrc
    URL https://github.com/vector-of-bool/cmrc/archive/refs/tags/2.0.1.tar.gz
    URL_HASH SHA256=edad5faaa0bea1df124b5e8cb00bf0adbd2faeccecd3b5c146796cbcb8b5b71b
    DOWNLOAD_ONLY YES
)

CPMAddPackage(
    NAME ReactivePlusPlus
    URL https://github.com/AlexInLog/ReactivePlusPlus/archive/refs/tags/v2.2.3.tar.gz
    URL_HASH SHA256=d66507538e75f61569acbb4d5e26e18adf839791120688693d2fe8da955236ea
)

CPMAddPackage(
    NAME tomlplusplus
    URL https://github.com/marzer/tomlplusplus/archive/refs/tags/v3.4.0.tar.gz
    URL_HASH SHA256=8517f65938a4faae9ccf8ebb36631a38c1cadfb5efa85d9a72e15b9e97d25155
    OPTIONS
        "TOMLPLUSPLUS_BUILD_TESTS OFF"
        "TOMLPLUSPLUS_BUILD_EXAMPLES OFF"
)

CPMAddPackage(
    NAME reflectcpp
    URL https://github.com/getml/reflect-cpp/archive/refs/tags/v0.25.0.tar.gz
    URL_HASH SHA256=de74d3793fd3dde9105ebe0f40bffb28df7009d59e0714389e4d29fcb46a1a3f
    OPTIONS
        "REFLECTCPP_JSON OFF"
        "REFLECTCPP_INSTALL OFF"
)

CPMAddPackage(
    NAME plog
    URL https://github.com/SergiusTheBest/plog/archive/refs/tags/1.1.11.tar.gz
    URL_HASH SHA256=d60b8b35f56c7c852b7f00f58cbe9c1c2e9e59566c5b200512d0cdbb6309a7c2
    OPTIONS
        "PLOG_BUILD_SAMPLES OFF"
        "PLOG_BUILD_TESTS OFF"
        "PLOG_INSTALL OFF"
)

CPMAddPackage(
    NAME mdns
    URL https://github.com/mjansson/mdns/archive/refs/tags/1.4.3.tar.gz
    URL_HASH SHA256=be1fd8e35599cb7de179decbd0633c121d11a2dcb9cc193ff5c590bd0d480483
    OPTIONS
        "MDNS_BUILD_EXAMPLE OFF"
)
