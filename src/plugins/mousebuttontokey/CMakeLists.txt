# SPDX-FileCopyrightText: 2022 David Redondo <kde@david-redono.de>
# SPDX-License-Identifier: BSD-3-Clause

kcoreaddons_add_plugin(MouseButtonToKeyPlugin INSTALL_NAMESPACE "kwin/plugins")

target_sources(MouseButtonToKeyPlugin PRIVATE
    main.cpp
    mousebuttonrebindfilter.cpp
)
target_link_libraries(MouseButtonToKeyPlugin PRIVATE kwin)

