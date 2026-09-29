# This file is part of Telegram Desktop,
# the official desktop application for the Telegram messaging service.
#
# For license and copyright information please follow this link:
# https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL

function(generate_lang target_name lang_file src_loc)
    set(gen_dst ${CMAKE_CURRENT_BINARY_DIR}/gen)
    file(MAKE_DIRECTORY ${gen_dst})

    set(gen_timestamp ${gen_dst}/lang_auto.timestamp)
    set(gen_keys ${gen_dst}/lang_auto_keys.h)
    set(gen_files
        ${gen_dst}/lang_auto.cpp
        ${gen_dst}/lang_auto.h
        ${gen_dst}/lang_auto_counts.h
        ${gen_keys}
    )

    add_custom_command(
    OUTPUT
        ${gen_timestamp}
    BYPRODUCTS
        ${gen_files}
    COMMAND
        codegen_lang
        -o${gen_dst}
        ${lang_file}
    COMMENT "Generating lang (${target_name})"
    DEPENDS
        codegen_lang
        ${lang_file}
    )
    generate_target(${target_name} lang ${gen_timestamp} "${gen_files}" ${gen_dst})

    # 上游的 subsets 扫描器只识别 lng_ 前缀的键，extras_ 前缀的键不会
    # 进入子集头，Ninja 生成器下引用它们的源码无法编译，故禁用
    # subsets，三平台统一使用完整的 lang_auto_keys.h。
endfunction()
