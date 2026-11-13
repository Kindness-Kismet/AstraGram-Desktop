# This file is part of Telegram Desktop,
# the official desktop application for the Telegram messaging service.
#
# For license and copyright information please follow this link:
# https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL

add_library(td_gram OBJECT)
init_non_host_target(td_gram)
add_library(tdesktop::td_gram ALIAS td_gram)

target_precompile_headers(td_gram PRIVATE ${src_loc}/gram/gram_pch.h)
nice_target_sources(td_gram ${src_loc}
PRIVATE
    gram/api/gram_api_account.cpp
    gram/api/gram_api_account.h
    gram/api/gram_api_emulate.cpp
    gram/api/gram_api_emulate.h
    gram/api/gram_api_nft.cpp
    gram/api/gram_api_nft.h
    gram/api/gram_api_rates.cpp
    gram/api/gram_api_rates.h
    gram/api/gram_api_request.cpp
    gram/api/gram_api_request.h
    gram/api/gram_api_stream.cpp
    gram/api/gram_api_stream.h
    gram/gram_boc.cpp
    gram/gram_boc.h
    gram/gram_pch.h
)

target_include_directories(td_gram
PUBLIC
    ${src_loc}
)

target_link_libraries(td_gram
PUBLIC
    desktop-app::lib_base
)
