/*
 * Copyright (c) 2026 The reone project contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <array>
#include <string_view>

namespace reone::test {

// Literal jump-table observations, not generated from the decoder's arithmetic.
// Index 0 is ordinal 1000. Both examined binaries agree at all 728 slots.
// Provenance and the four cut039 exceptions: camera-animation-evidence.md.
inline constexpr std::array<std::string_view, 728> kBinaryCameraAnimationNames {{
    "cut001", "cut002", "cut003", "cut004", "cut005", "cut006", "cut007", "cut008", // 1000
    "cut009", "cut010", "cut011", "cut012", "cut013", "cut014", "cut015", "cut016", // 1008
    "cut017", "cut018", "cut019", "cut020", "cut021", "cut022", "cut023", "cut024", // 1016
    "cut025", "cut026", "cut027", "cut028", "cut039", "cut030", "cut031", "cut032", // 1024
    "cut033", "cut034", "cut035", "cut036", "cut037", "cut038", "cut039", "cut040", // 1032
    "cut041", "cut042", "cut043", "cut044", "cut045", "cut046", "cut047", "cut048", // 1040
    "cut049", "cut050", "cut051", "cut052", "cut053", "cut054", "cut055", "cut056", // 1048
    "cut057", "cut058", "cut059", "cut060", "cut061", "cut062", "cut063", "cut064", // 1056
    "cut065", "cut066", "cut067", "cut068", "cut069", "cut070", "cut071", "cut072", // 1064
    "cut073", "cut074", "cut075", "cut076", "cut077", "cut078", "cut079", "cut080", // 1072
    "cut081", "cut082", "cut083", "cut084", "cut085", "cut086", "cut087", "cut088", // 1080
    "cut089", "cut090", "cut091", "cut092", "cut093", "cut094", "cut095", "cut096", // 1088
    "cut097", "cut098", "cut099", "cut100", "cut101", "cut102", "cut103", "cut104", // 1096
    "cut105", "cut106", "cut107", "cut108", "cut109", "cut110", "cut111", "cut112", // 1104
    "cut113", "cut114", "cut115", "cut116", "cut117", "cut118", "cut119", "cut120", // 1112
    "cut121", "cut122", "cut123", "cut124", "cut125", "cut126", "cut127", "cut128", // 1120
    "none", "none", "none", "none", "none", "none", "none", "none", // 1128
    "none", "none", "none", "none", "none", "none", "none", "none", // 1136
    "none", "none", "none", "none", "none", "none", "none", "none", // 1144
    "none", "none", "none", "none", "none", "none", "none", "none", // 1152
    "none", "none", "none", "none", "none", "none", "none", "none", // 1160
    "none", "none", "none", "none", "none", "none", "none", "none", // 1168
    "none", "none", "none", "none", "none", "none", "none", "none", // 1176
    "none", "none", "none", "none", "none", "none", "none", "none", // 1184
    "none", "none", "none", "none", "none", "none", "none", "none", // 1192
    "cut001w", "cut002w", "cut003w", "cut004w", "cut005w", "cut006w", "cut007w", "cut008w", // 1200
    "cut009w", "cut010w", "cut011w", "cut012w", "cut013w", "cut014w", "cut015w", "cut016w", // 1208
    "cut017w", "cut018w", "cut019w", "cut020w", "cut021w", "cut022w", "cut023w", "cut024w", // 1216
    "cut025w", "cut026w", "cut027w", "cut028w", "cut039w", "cut030w", "cut031w", "cut032w", // 1224
    "cut033w", "cut034w", "cut035w", "cut036w", "cut037w", "cut038w", "cut039w", "cut040w", // 1232
    "cut041w", "cut042w", "cut043w", "cut044w", "cut045w", "cut046w", "cut047w", "cut048w", // 1240
    "cut049w", "cut050w", "cut051w", "cut052w", "cut053w", "cut054w", "cut055w", "cut056w", // 1248
    "cut057w", "cut058w", "cut059w", "cut060w", "cut061w", "cut062w", "cut063w", "cut064w", // 1256
    "cut065w", "cut066w", "cut067w", "cut068w", "cut069w", "cut070w", "cut071w", "cut072w", // 1264
    "cut073w", "cut074w", "cut075w", "cut076w", "cut077w", "cut078w", "cut079w", "cut080w", // 1272
    "cut081w", "cut082w", "cut083w", "cut084w", "cut085w", "cut086w", "cut087w", "cut088w", // 1280
    "cut089w", "cut090w", "cut091w", "cut092w", "cut093w", "cut094w", "cut095w", "cut096w", // 1288
    "cut097w", "cut098w", "cut099w", "cut100w", "cut101w", "cut102w", "cut103w", "cut104w", // 1296
    "cut105w", "cut106w", "cut107w", "cut108w", "cut109w", "cut110w", "cut111w", "cut112w", // 1304
    "cut113w", "cut114w", "cut115w", "cut116w", "cut117w", "cut118w", "cut119w", "cut120w", // 1312
    "cut121w", "cut122w", "cut123w", "cut124w", "cut125w", "cut126w", "cut127w", "cut128w", // 1320
    "none", "none", "none", "none", "none", "none", "none", "none", // 1328
    "none", "none", "none", "none", "none", "none", "none", "none", // 1336
    "none", "none", "none", "none", "none", "none", "none", "none", // 1344
    "none", "none", "none", "none", "none", "none", "none", "none", // 1352
    "none", "none", "none", "none", "none", "none", "none", "none", // 1360
    "none", "none", "none", "none", "none", "none", "none", "none", // 1368
    "none", "none", "none", "none", "none", "none", "none", "none", // 1376
    "none", "none", "none", "none", "none", "none", "none", "none", // 1384
    "none", "none", "none", "none", "none", "none", "none", "none", // 1392
    "cut001l", "cut002l", "cut003l", "cut004l", "cut005l", "cut006l", "cut007l", "cut008l", // 1400
    "cut009l", "cut010l", "cut011l", "cut012l", "cut013l", "cut014l", "cut015l", "cut016l", // 1408
    "cut017l", "cut018l", "cut019l", "cut020l", "cut021l", "cut022l", "cut023l", "cut024l", // 1416
    "cut025l", "cut026l", "cut027l", "cut028l", "cut039l", "cut030l", "cut031l", "cut032l", // 1424
    "cut033l", "cut034l", "cut035l", "cut036l", "cut037l", "cut038l", "cut039l", "cut040l", // 1432
    "cut041l", "cut042l", "cut043l", "cut044l", "cut045l", "cut046l", "cut047l", "cut048l", // 1440
    "cut049l", "cut050l", "cut051l", "cut052l", "cut053l", "cut054l", "cut055l", "cut056l", // 1448
    "cut057l", "cut058l", "cut059l", "cut060l", "cut061l", "cut062l", "cut063l", "cut064l", // 1456
    "cut065l", "cut066l", "cut067l", "cut068l", "cut069l", "cut070l", "cut071l", "cut072l", // 1464
    "cut073l", "cut074l", "cut075l", "cut076l", "cut077l", "cut078l", "cut079l", "cut080l", // 1472
    "cut081l", "cut082l", "cut083l", "cut084l", "cut085l", "cut086l", "cut087l", "cut088l", // 1480
    "cut089l", "cut090l", "cut091l", "cut092l", "cut093l", "cut094l", "cut095l", "cut096l", // 1488
    "cut097l", "cut098l", "cut099l", "cut100l", "cut101l", "cut102l", "cut103l", "cut104l", // 1496
    "cut105l", "cut106l", "cut107l", "cut108l", "cut109l", "cut110l", "cut111l", "cut112l", // 1504
    "cut113l", "cut114l", "cut115l", "cut116l", "cut117l", "cut118l", "cut119l", "cut120l", // 1512
    "cut121l", "cut122l", "cut123l", "cut124l", "cut125l", "cut126l", "cut127l", "cut128l", // 1520
    "none", "none", "none", "none", "none", "none", "none", "none", // 1528
    "none", "none", "none", "none", "none", "none", "none", "none", // 1536
    "none", "none", "none", "none", "none", "none", "none", "none", // 1544
    "none", "none", "none", "none", "none", "none", "none", "none", // 1552
    "none", "none", "none", "none", "none", "none", "none", "none", // 1560
    "none", "none", "none", "none", "none", "none", "none", "none", // 1568
    "none", "none", "none", "none", "none", "none", "none", "none", // 1576
    "none", "none", "none", "none", "none", "none", "none", "none", // 1584
    "none", "none", "none", "none", "none", "none", "none", "none", // 1592
    "cut001wl", "cut002wl", "cut003wl", "cut004wl", "cut005wl", "cut006wl", "cut007wl", "cut008wl", // 1600
    "cut009wl", "cut010wl", "cut011wl", "cut012wl", "cut013wl", "cut014wl", "cut015wl", "cut016wl", // 1608
    "cut017wl", "cut018wl", "cut019wl", "cut020wl", "cut021wl", "cut022wl", "cut023wl", "cut024wl", // 1616
    "cut025wl", "cut026wl", "cut027wl", "cut028wl", "cut039wl", "cut030wl", "cut031wl", "cut032wl", // 1624
    "cut033wl", "cut034wl", "cut035wl", "cut036wl", "cut037wl", "cut038wl", "cut039wl", "cut040wl", // 1632
    "cut041wl", "cut042wl", "cut043wl", "cut044wl", "cut045wl", "cut046wl", "cut047wl", "cut048wl", // 1640
    "cut049wl", "cut050wl", "cut051wl", "cut052wl", "cut053wl", "cut054wl", "cut055wl", "cut056wl", // 1648
    "cut057wl", "cut058wl", "cut059wl", "cut060wl", "cut061wl", "cut062wl", "cut063wl", "cut064wl", // 1656
    "cut065wl", "cut066wl", "cut067wl", "cut068wl", "cut069wl", "cut070wl", "cut071wl", "cut072wl", // 1664
    "cut073wl", "cut074wl", "cut075wl", "cut076wl", "cut077wl", "cut078wl", "cut079wl", "cut080wl", // 1672
    "cut081wl", "cut082wl", "cut083wl", "cut084wl", "cut085wl", "cut086wl", "cut087wl", "cut088wl", // 1680
    "cut089wl", "cut090wl", "cut091wl", "cut092wl", "cut093wl", "cut094wl", "cut095wl", "cut096wl", // 1688
    "cut097wl", "cut098wl", "cut099wl", "cut100wl", "cut101wl", "cut102wl", "cut103wl", "cut104wl", // 1696
    "cut105wl", "cut106wl", "cut107wl", "cut108wl", "cut109wl", "cut110wl", "cut111wl", "cut112wl", // 1704
    "cut113wl", "cut114wl", "cut115wl", "cut116wl", "cut117wl", "cut118wl", "cut119wl", "cut120wl", // 1712
    "cut121wl", "cut122wl", "cut123wl", "cut124wl", "cut125wl", "cut126wl", "cut127wl", "cut128wl", // 1720
}};

} // namespace reone::test
