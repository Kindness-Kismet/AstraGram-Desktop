/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "tests/test_iv_editor_state_common.h"

namespace TestIvEditorState {

[[nodiscard]] Report RemovalBlocksRangeAndDirection() {
	return RunRemovals({
		{
			.start = { "R01", kFlat4, 0, Blocks(Root(), 1, 3) },
			.returned = 1,
			.page = Flat({ u"A"_q, u"D"_q }),
			.state = R"(1=text:1"D" ; live P0 P1 nodes=2)",
		},
		{
			.start = { "R02", kFlat4, 0, Blocks(Root(), 1, 3) },
			.forward = false,
			.returned = 0,
			.page = Flat({ u"A"_q, u"D"_q }),
			.state = R"(0=text:0"A" ; live P0 P1 nodes=2)",
		},
		{
			.start = { "R03", kFlat4, 0, Blocks(Root(), 0, 1) },
			.returned = 0,
			.page = Flat({ u"B"_q, u"C"_q, u"D"_q }),
			.state = R"(0=text:0"B" ; live P0 P1 P2 nodes=3)",
		},
		{
			.start = { "R04", kFlat4, 0, Blocks(Root(), 3, 4) },
			.forward = false,
			.returned = 2,
			.page = Flat({ u"A"_q, u"B"_q, u"C"_q }),
			.state = R"(2=text:2"C" ; live P0 P1 P2 nodes=3)",
		},
		{
			.start = { "R05", kFlat4, 0, Blocks(Root(), 0, 4) },
			.returned = 0,
			.page = Flat({ QString() }),
			.state = R"(0=text:0"" ; live P0 nodes=1)",
		},
		{
			.start = { "R06", kFlat4, 2, Blocks(Root(), 0, 4) },
			.forward = false,
			.returned = 0,
			.page = Flat({ QString() }),
			.state = R"(0=text:0"" ; live P0 nodes=1)",
		},
	});
}

[[nodiscard]] Report RemovalBlocksActiveLeafKept() {
	return RunRemovals({
		{
			.start = { "R07", kFlat4, 1, Blocks(Root(), 2, 4) },
			.returned = 1,
			.page = Flat({ u"A"_q, u"B"_q }),
			.state = R"(1=text:1"B" ; live P0 P1 nodes=2)",
		},
		{
			.start = { "R08", kFlat4, 0, Blocks(Root(), 3, 4) },
			.returned = 0,
			.page = Flat({ u"A"_q, u"B"_q, u"C"_q }),
			.state = R"(0=text:0"A" ; live P0 P1 P2 nodes=3)",
		},
	});
}

[[nodiscard]] Report RemovalBlocksActiveLeafRemoved() {
	return RunRemovals({
		{
			.start = { "R09", kFlat4, 2, Blocks(Root(), 2, 4) },
			.returned = 0,
			.page = Flat({ u"A"_q, u"B"_q }),
			.state = R"(0=text:0"A" ; live P0 P1 nodes=2)",
		},
		{
			.start = { "R10", kFlat4, 3, Blocks(Root(), 0, 1) },
			.forward = false,
			.returned = 2,
			.page = Flat({ u"B"_q, u"C"_q, u"D"_q }),
			.state = R"(2=text:2"D" ; live P0 P1 P2 nodes=3)",
		},
	});
}

[[nodiscard]] Report RemovalBlocksLeadingPlannedFocus() {
	const auto lastThree = Flat({ u"C"_q, u"D"_q, u"E"_q });
	return RunRemovals({
		{
			.start = { "R11", kFlat4, 0, Blocks(Root(), 0, 1) },
			.forward = false,
			.returned = 0,
			.page = Flat({ u"B"_q, u"C"_q, u"D"_q }),
			.state = R"(0=text:0"B" ; live P0 P1 P2 nodes=3)",
		},
		{
			.start = { "R12", kFlat5, 2, Blocks(Root(), 0, 2) },
			.forward = false,
			.returned = 0,
			.page = lastThree,
			.state = R"(0=text:0"C" ; live P0 P1 P2 nodes=3)",
		},
		{
			.start = { "R13", kFlat5, 0, Blocks(Root(), 0, 2) },
			.forward = false,
			.returned = 0,
			.page = lastThree,
			.state = R"(0=text:0"C" ; live P0 P1 P2 nodes=3)",
		},
		{
			.start = { "R14", kFlat5, 4, Blocks(Root(), 0, 2) },
			.forward = false,
			.returned = 2,
			.page = lastThree,
			.state = R"(2=text:2"E" ; live P0 P1 P2 nodes=3)",
		},
	});
}

[[nodiscard]] Report RemovalQuoteChildren() {
	const auto quoted = [](std::initializer_list<QString> texts) {
		return Framed(Quote(Paragraphs(texts), u"cap"_q));
	};
	return RunRemovals({
		{
			.start = { "R15", kQuoted, 0, Blocks(Children(1), 1, 2) },
			.returned = 2,
			.page = quoted({ u"q0"_q, u"q2"_q }),
			.state = R"(2=text:1/1"q2" ; live P0 Q1 P2 nodes=5)",
			.leaves = R"(text:0"A" text:1/0"q0" text:1/1"q2" caption:1"cap")"
				R"( text:2"C")",
		},
		{
			.start = { "R16", kQuoted, 0, Blocks(Children(1), 1, 2) },
			.forward = false,
			.returned = 1,
			.page = quoted({ u"q0"_q, u"q2"_q }),
			.state = R"(1=text:1/0"q0" ; live P0 Q1 P2 nodes=5)",
		},
		{
			.start = { "R17", kQuoted, 1, Blocks(Children(1), 0, 1) },
			.forward = false,
			.returned = 1,
			.page = quoted({ u"q1"_q, u"q2"_q }),
			.state = R"(1=text:1/0"q1" ; live P0 Q1 P2 nodes=5)",
		},
		{
			.start = { "R18", kQuoted, 0, Blocks(Children(1), 2, 3) },
			.returned = 1,
			.page = quoted({ u"q0"_q, u"q1"_q }),
			.state = R"(1=text:1/0"q0" ; live P0 Q1 P2 nodes=5)",
		},
		{
			.start = { "R19", kQuoted, 0, Blocks(Children(1), 0, 3) },
			.returned = 1,
			.page = quoted({}),
			.state = R"(1=text:1"" ; live P0 Q1 P2 nodes=4)",
		},
		{
			.start = { "R20", kQuoted, 0, Blocks(Children(1), 0, 3) },
			.forward = false,
			.returned = 1,
			.page = quoted({}),
			.state = R"(1=text:1"" ; live P0 Q1 P2 nodes=4)",
		},
		{
			.start = { "R21", kQuoted, 2, Blocks(Root(), 1, 2) },
			.returned = 1,
			.page = Flat({ u"A"_q, u"C"_q }),
			.state = R"(1=text:1"C" ; live P0 P1 nodes=2)",
		},
	});
}

[[nodiscard]] Report RemovalListItems() {
	return RunRemovals({
		{
			.start = { "R22", kListed, 0, Items(1, 1, 2) },
			.returned = 2,
			.page = Framed(List({ u"i0"_q, u"i2"_q })),
			.state = R"(2=item:1#1"i2" ; live P0 L1 P2 nodes=4)",
		},
		{
			.start = { "R23", kListed, 0, Items(1, 1, 2) },
			.forward = false,
			.returned = 1,
			.page = Framed(List({ u"i0"_q, u"i2"_q })),
			.state = R"(1=item:1#0"i0" ; live P0 L1 P2 nodes=4)",
		},
		{
			.start = { "R24", kListed, 1, Items(1, 0, 1) },
			.returned = 1,
			.page = Framed(List({ u"i1"_q, u"i2"_q })),
			.state = R"(1=item:1#0"i1" ; live P0 L1 P2 nodes=4)",
		},
		{
			.start = { "R25", kListed, 1, Items(1, 0, 1) },
			.forward = false,
			.returned = 0,
			.page = Framed(List({ u"i1"_q, u"i2"_q })),
			.state = R"(0=text:0"A" ; live P0 L1 P2 nodes=4)",
		},
		{
			.start = { "R26", kListed, 3, Items(1, 2, 3) },
			.returned = 3,
			.page = Framed(List({ u"i0"_q, u"i1"_q })),
			.state = R"(3=text:2"C" ; live P0 L1 P2 nodes=4)",
		},
		{
			.start = { "R27", kListed, 3, Items(1, 2, 3) },
			.forward = false,
			.returned = 2,
			.page = Framed(List({ u"i0"_q, u"i1"_q })),
			.state = R"(2=item:1#1"i1" ; live P0 L1 P2 nodes=4)",
		},
		{
			.start = { "R28", kListed, 2, Items(1, 0, 3) },
			.returned = 1,
			.page = Flat({ u"A"_q, u"C"_q }),
			.state = R"(1=text:1"C" ; live P0 P1 nodes=2)",
		},
		{
			.start = { "R29", kListed, 2, Items(1, 0, 3) },
			.forward = false,
			.returned = 0,
			.page = Flat({ u"A"_q, u"C"_q }),
			.state = R"(0=text:0"A" ; live P0 P1 nodes=2)",
		},
	});
}

[[nodiscard]] Report RemovalListItemsLeadingPlannedFocus() {
	const auto lastItems = Page({
		List({
			Item(u"i1"_q, Paragraphs({ u"x1"_q })),
			Item(u"i2"_q),
		}),
		Paragraph(u"C"_q),
	});
	const auto withoutList = Page({
		Paragraph(u"M"_q),
		List({ u"b0"_q, u"b1"_q }),
	});
	return RunRemovals({
		{
			.start = { "R68", kListFirst, 3, Items(0, 0, 1) },
			.forward = false,
			.returned = 2,
			.page = lastItems,
			.state = R"(2=item:0#1"i2" ; live L0 P1 nodes=4)",
		},
		{
			.start = { "R69", kListFirst, 0, Items(0, 0, 1) },
			.forward = false,
			.returned = 0,
			.page = lastItems,
			.state = R"(0=item:0#0"i1" ; live L0 P1 nodes=4)",
		},
		{
			.start = { "R70", kListFirst, 2, Items(0, 0, 1) },
			.forward = false,
			.returned = 1,
			.page = lastItems,
			.state = R"(1=text:0.0/0"x1" ; live L0 P1 nodes=4)",
		},
		{
			.start = { "R71", kTwoLists, 2, Items(0, 0, 2) },
			.forward = false,
			.returned = 0,
			.page = withoutList,
			.state = R"(0=text:0"M" ; live P0 L1 nodes=3)",
		},
		{
			.start = { "R72", kTwoLists, 1, Items(0, 0, 2) },
			.forward = false,
			.returned = 0,
			.page = withoutList,
			.state = R"(0=text:0"M" ; live P0 L1 nodes=3)",
		},
	});
}

[[nodiscard]] Report RemovalListItemChildren() {
	return RunRemovals({
		{
			.start = { "R30", kNested, 0, Blocks(ItemChildren(1, 0), 0, 1) },
			.returned = 2,
			.page = Framed(List({
				Item(u"i0"_q, Paragraphs({ u"x1"_q })),
				Item(u"i1"_q),
			})),
			.state = R"(2=text:1.0/0"x1" ; live P0 L1 P2 nodes=5)",
		},
		{
			.start = { "R31", kNested, 0, Blocks(ItemChildren(1, 0), 0, 2) },
			.forward = false,
			.returned = 1,
			.page = Framed(List({ u"i0"_q, u"i1"_q })),
			.state = R"(1=item:1#0"i0" ; live P0 L1 P2 nodes=4)",
		},
		{
			.start = { "R32", kNested, 0, Items(1, 0, 1) },
			.returned = 1,
			.page = Framed(List({ u"i1"_q })),
			.state = R"(1=item:1#0"i1" ; live P0 L1 P2 nodes=3)",
		},
	});
}

[[nodiscard]] Report RemovalTableRows() {
	const auto withoutMiddle = Framed(u"T"_q, Grid({ 0, 2 }, { 0, 1 }));
	return RunRemovals({
		{
			.start = { "R33", kTabled, 0, Rows(1, 1, 2) },
			.returned = 4,
			.page = withoutMiddle,
			.state = R"(4=cell:1#1,0"r2c0" ; live P0 T1 P2 nodes=7)",
		},
		{
			.start = { "R34", kTabled, 0, Rows(1, 1, 2) },
			.forward = false,
			.returned = 3,
			.page = withoutMiddle,
			.state = R"(3=cell:1#0,1"r0c1" ; live P0 T1 P2 nodes=7)",
		},
		{
			.start = { "R35", kTabled, 0, Rows(1, 0, 1) },
			.forward = false,
			.returned = 2,
			.page = Framed(u"T"_q, Grid({ 1, 2 }, { 0, 1 })),
			.state = R"(2=cell:1#0,0"r1c0" ; live P0 T1 P2 nodes=7)",
		},
		{
			.start = { "R36", kTabled, 0, Rows(1, 2, 3) },
			.returned = 5,
			.page = Framed(u"T"_q, Grid({ 0, 1 }, { 0, 1 })),
			.state = R"(5=cell:1#1,1"r1c1" ; live P0 T1 P2 nodes=7)",
		},
		{
			.start = { "R37", kTabled, 0, Rows(1, 0, 3) },
			.returned = 1,
			.page = Framed(u"T"_q, {}),
			.state = R"(1=text:1"T" ; live P0 T1 P2 nodes=3)",
		},
		{
			.start = { "R38", kUntitled, 0, Rows(1, 0, 2) },
			.returned = 1,
			.page = Flat({ u"A"_q, u"C"_q }),
			.state = R"(1=text:1"C" ; live P0 P1 nodes=2)",
		},
		{
			.start = { "R39", kTabled, 4, Rows(1, 1, 2) },
			.returned = 4,
			.page = withoutMiddle,
			.state = R"(4=cell:1#1,0"r2c0" ; live P0 T1 P2 nodes=7)",
		},
	});
}

[[nodiscard]] RemovalCase RowSpanMovedRemoval(const char *id) {
	return {
		.start = { id, kRowSpanRows, 0, Rows(0, 0, 1) },
		.returned = 1,
		.page = Page({ Table(u"T"_q, {
			{ { u"a"_q }, { u"c"_q } },
			{ { u"d"_q }, { u"e"_q } },
		}) }),
		.state = R"(1=cell:0#0,0"a" ; live T0 nodes=5)",
		.spans = "1x1 1x1|1x1 1x1",
		.grid = R"("a"@r0-1c0-1 "c"@r0-1c1-2 "d"@r1-2c0-1 "e"@r1-2c1-2)",
	};
}

[[nodiscard]] RemovalCase RowSpanShortenedRemoval(const char *id) {
	return {
		.start = { id, kRowSpanRows, 0, Rows(0, 1, 2) },
		.returned = 3,
		.page = Page({ Table(u"T"_q, {
			{ { u"a"_q }, { u"b"_q } },
			{ { u"d"_q }, { u"e"_q } },
		}) }),
		.state = R"(3=cell:0#1,0"d" ; live T0 nodes=5)",
		.spans = "1x1 1x1|1x1 1x1",
		.grid = R"("a"@r0-1c0-1 "b"@r0-1c1-2 "d"@r1-2c0-1 "e"@r1-2c1-2)",
	};
}

[[nodiscard]] RemovalCase OmittedSpanRemoval(const char *id) {
	return {
		.start = { id, kOmittedSpan, 0, Rows(0, 0, 1) },
		.returned = 1,
		.page = Page({ Table(u"T"_q, {
			{ { QString() }, { u"a"_q, 1, 2 } },
			{ { u"d"_q } },
		}) }),
		.state = R"(1=cell:0#0,0"" ; live T0 nodes=4)",
		.spans = "1x1 1x2|1x1",
		.grid = R"(""@r0-1c0-1 "a"@r0-2c1-2 "d"@r1-2c0-1)",
	};
}

[[nodiscard]] Report RemovalTableRowsMerged() {
	return RunRemovals({
		RowSpanMovedRemoval("R40"),
		RowSpanShortenedRemoval("R73"),
		{
			.start = { "R74", kRowSpanThree, 0, Rows(0, 1, 2) },
			.returned = 3,
			.page = Page({ Table(u"T"_q, {
				{ { u"a"_q, 1, 2 }, { u"b"_q } },
				{ { u"d"_q } },
				{ { u"e"_q }, { u"f"_q } },
			}) }),
			.state = R"(3=cell:0#1,0"d" ; live T0 nodes=6)",
			.spans = "1x2 1x1|1x1|1x1 1x1",
			.grid = R"("a"@r0-2c0-1 "b"@r0-1c1-2 "d"@r1-2c1-2)"
				R"( "e"@r2-3c0-1 "f"@r2-3c1-2)",
		},
		{
			.start = { "R75", kRowSpanThree, 0, Rows(0, 0, 1) },
			.returned = 1,
			.page = Page({ Table(u"T"_q, {
				{ { u"a"_q, 1, 2 }, { u"c"_q } },
				{ { u"d"_q } },
				{ { u"e"_q }, { u"f"_q } },
			}) }),
			.state = R"(1=cell:0#0,0"a" ; live T0 nodes=6)",
			.spans = "1x2 1x1|1x1|1x1 1x1",
			.grid = R"("a"@r0-2c0-1 "c"@r0-1c1-2 "d"@r1-2c1-2)"
				R"( "e"@r2-3c0-1 "f"@r2-3c1-2)",
		},
		{
			.start = { "R76", kRowMerged, 0, Rows(0, 0, 1) },
			.returned = 1,
			.page = Page({ Table(u"T"_q, {
				{ { u"c"_q }, { u"m"_q }, { u"d"_q } },
			}) }),
			.state = R"(1=cell:0#0,0"c" ; live T0 nodes=4)",
			.spans = "1x1 1x1 1x1",
			.grid = R"("c"@r0-1c0-1 "m"@r0-1c1-2 "d"@r0-1c2-3)",
		},
		{
			.start = { "R77", kRowSpanThree, 0, Rows(0, 1, 3) },
			.returned = 3,
			.page = Page({ Table(u"T"_q, {
				{ { u"a"_q }, { u"b"_q } },
				{ { u"e"_q }, { u"f"_q } },
			}) }),
			.state = R"(3=cell:0#1,0"e" ; live T0 nodes=5)",
			.spans = "1x1 1x1|1x1 1x1",
			.grid = R"("a"@r0-1c0-1 "b"@r0-1c1-2 "e"@r1-2c0-1 "f"@r1-2c1-2)",
		},
		{
			.start = { "R80", kEmptyTarget, 0, Rows(0, 0, 2) },
			.returned = 1,
			.page = Page({ Table(u"T"_q, {
				{ { u"a"_q, 2 }, { u"b"_q } },
			}) }),
			.state = R"(1=cell:0#0,0"a" ; live T0 nodes=3)",
			.spans = "2x1 1x1",
			.grid = R"("a"@r0-1c0-2 "b"@r0-1c2-3)",
		},
		{
			.start = { "R81", kTrailingSpans, 0, Rows(0, 1, 2) },
			.returned = 5,
			.page = WithDecoratedCell(
				Page({ Table(u"T"_q, {
					{ { u"p"_q, 1, 2 }, { u"q"_q }, { u"r"_q }, { u"s"_q } },
					{ { u"c"_q }, { u"a"_q }, { u"b"_q } },
				}) }),
				1,
				1),
			.state = R"(5=cell:0#1,0"c" ; live T0 nodes=8)",
			.spans = "1x2 1x1 1x1 1x1|1x1 1x1 1x1",
			.grid = R"("p"@r0-2c0-1 "q"@r0-1c1-2 "r"@r0-1c2-3 "s"@r0-1c3-4)"
				R"( "c"@r1-2c1-2 "a"@r1-2c2-3 "b"@r1-2c3-4)",
		},
		{
			.start = { "R82", kSpanGap, 0, Rows(0, 0, 1) },
			.returned = 1,
			.page = Page({ Table(u"T"_q, {
				{ { QString() }, { u"a"_q } },
			}) }),
			.state = R"(1=cell:0#0,0"" ; live T0 nodes=3)",
			.spans = "1x1 1x1",
			.grid = R"(""@r0-1c0-1 "a"@r0-1c1-2)",
		},
		{
			.start = { "R83", kUnorderedTarget, 0, Rows(0, 0, 1) },
			.returned = 1,
			.page = Page({ Table(u"T"_q, {
				{ { u"v"_q }, { u"m"_q }, { u"w"_q, 2 } },
			}) }),
			.state = R"(1=cell:0#0,0"v" ; live T0 nodes=4)",
			.spans = "1x1 1x1 2x1",
			.grid = R"("v"@r0-1c0-1 "m"@r0-1c1-2 "w"@r0-1c2-4)",
		},
		{
			.start = { "R84", kLongSpan, 0, Rows(0, 0, 1) },
			.returned = 1,
			.page = Page({ Table(u"T"_q, {
				{ { u"a"_q, 1, 98 }, { u"c"_q } },
			}) }),
			.state = R"(1=cell:0#0,0"a" ; live T0 nodes=3)",
			.spans = "1x1 1x1",
			.grid = R"("a"@r0-1c0-1 "c"@r0-1c1-2)",
		},
		{
			.start = { "R85", kUnplacedSpan, 0, Rows(0, 0, 1) },
			.returned = 1,
			.page = Page({ Table(u"T"_q, {
				{ { u"c"_q }, { u"a"_q } },
			}) }),
			.state = R"(1=cell:0#0,0"c" ; live T0 nodes=3)",
			.spans = "1x1",
			.grid = R"("c"@r0-1c0-1)",
		},
		OmittedSpanRemoval("R86"),
		{
			.start = { "R87", kOmittedSpanOneColumn, 0, Rows(0, 0, 1) },
			.returned = 1,
			.page = Page({ Table(u"T"_q, {
				{ { QString() }, { u"a"_q, 1, 2 } },
				{ { u"d"_q } },
			}) }),
			.state = R"(1=cell:0#0,0"" ; live T0 nodes=4)",
			.spans = "1x1|1x1",
			.grid = R"(""@r0-1c0-1 "d"@r1-2c0-1)",
		},
	});
}

[[nodiscard]] Report RemovalTableRowsMenu() {
	const auto cases = std::vector<RemovalCase>{
		RowSpanMovedRemoval("R78"),
		RowSpanShortenedRemoval("R79"),
		OmittedSpanRemoval("R88"),
	};
	return RunCases(cases, CheckMenuRemoval);
}

[[nodiscard]] Report RemovalTableColumns() {
	const auto withoutMiddle = Framed(u"T"_q, Grid({ 0, 1, 2 }, { 0, 2 }));
	const auto withoutLast = Framed(u"T"_q, Grid({ 0, 1, 2 }, { 0, 1 }));
	const auto withoutFirst = Framed(u"T"_q, Grid({ 0, 1, 2 }, { 1, 2 }));
	return RunRemovals({
		{
			.start = { "R41", kTabled3, 0, Cells(1, 0, 3, 1, 2) },
			.returned = 3,
			.page = withoutMiddle,
			.state = R"(3=cell:1#0,1"r0c2" ; live P0 T1 P2 nodes=9)",
		},
		{
			.start = { "R42", kTabled3, 0, Cells(1, 0, 3, 1, 2) },
			.forward = false,
			.returned = 3,
			.page = withoutMiddle,
			.state = R"(3=cell:1#0,1"r0c2" ; live P0 T1 P2 nodes=9)",
		},
		{
			.start = { "R43", kTabled3, 0, Cells(1, 0, 3, 2, 3) },
			.returned = 2,
			.page = withoutLast,
			.state = R"(2=cell:1#0,0"r0c0" ; live P0 T1 P2 nodes=9)",
		},
		{
			.start = { "R44", kTabled3, 0, Cells(1, 0, 3, 2, 3) },
			.forward = false,
			.returned = 7,
			.page = withoutLast,
			.state = R"(7=cell:1#2,1"r2c1" ; live P0 T1 P2 nodes=9)",
		},
		{
			.start = { "R45", kTabled3, 0, Cells(1, 0, 3, 0, 1) },
			.forward = false,
			.returned = 2,
			.page = withoutFirst,
			.state = R"(2=cell:1#0,0"r0c1" ; live P0 T1 P2 nodes=9)",
		},
		{
			.start = { "R46", kTabled3, 3, Cells(1, 0, 3, 0, 1) },
			.returned = 2,
			.page = withoutFirst,
			.state = R"(2=cell:1#0,0"r0c1" ; live P0 T1 P2 nodes=9)",
		},
	});
}

[[nodiscard]] Report RemovalTableColumnsWhole() {
	return RunRemovals({
		{
			.start = { "R47", kTabled3, 0, Cells(1, 0, 3, 0, 3) },
			.returned = 2,
			.page = Framed(u"T"_q, { { { QString() } } }),
			.state = R"(2=cell:1#0,0"" ; live P0 T1 P2 nodes=4)",
		},
		{
			.start = { "R48", kUntitled, 0, Cells(1, 0, 2, 0, 2) },
			.returned = 1,
			.page = Flat({ u"A"_q, u"C"_q }),
			.state = R"(1=text:1"C" ; live P0 P1 nodes=2)",
		},
	});
}

[[nodiscard]] Report RemovalTableColumnsMerged() {
	return RunRemovals({
		{
			.start = { "R49", kColMerged, 0, Cells(0, 0, 2, 1, 2) },
			.returned = 2,
			.page = Page({ Table(u"T"_q, {
				{ { u"m"_q }, { u"x"_q } },
				{ { u"a"_q }, { u"c"_q } },
			}) }),
			.state = R"(2=cell:0#0,1"x" ; live T0 nodes=5)",
			.leaves = R"(text:0"T" cell:0#0,0"m" cell:0#0,1"x")"
				R"( cell:0#1,0"a" cell:0#1,1"c")",
			.spans = "1x1 1x1|1x1 1x1",
		},
		{
			.start = { "R50", kRowMerged, 0, Cells(0, 0, 2, 1, 2) },
			.returned = 2,
			.page = Page({ Table(u"T"_q, {
				{ { u"a"_q }, { u"b"_q } },
				{ { u"c"_q }, { u"d"_q } },
			}) }),
			.state = R"(2=cell:0#0,1"b" ; live T0 nodes=5)",
		},
		{
			.start = { "R51", kColMerged, 0, Cells(0, 0, 2, 0, 2) },
			.returned = 1,
			.page = Page({ Table(u"T"_q, { { { u"x"_q } }, { { u"c"_q } } }) }),
			.state = R"(1=cell:0#0,0"x" ; live T0 nodes=3)",
		},
	});
}

[[nodiscard]] Report RemovalRejected() {
	return RunRemovals({
		{
			.start = { "R52", kFlat4, 2, Blocks(Root(), 2, 5) },
			.page = kFlat4.page(),
			.state = kFlat4AtC,
		},
		{
			.start = { "R53", kFlat4, 2, Blocks(Root(), -1, 1) },
			.page = kFlat4.page(),
			.state = kFlat4AtC,
		},
		{
			.start = { "R54", kFlat4, 2, Blocks(Root(), 2, 2) },
			.page = kFlat4.page(),
			.state = kFlat4AtC,
		},
		{
			.start = { "R55", kFlat4, 2, Blocks(Children(0), 0, 1) },
			.page = kFlat4.page(),
			.state = kFlat4AtC,
		},
		{
			.start = { "R56", kFlat4, 2, Blocks(Children(7), 0, 1) },
			.page = kFlat4.page(),
			.state = kFlat4AtC,
		},
		{
			.start = { "R57", kWithU, 1, Blocks(Root(), 0, 2) },
			.page = kWithU.page(),
			.state = R"(1=text:2"C" ; live P0 P2 nodes=2)",
		},
		{
			.start = { "R58", kListed, 2, Items(0, 0, 1) },
			.page = kListed.page(),
			.state = kListedAtI1,
		},
		{
			.start = { "R59", kListed, 2, Items(1, 1, 4) },
			.page = kListed.page(),
			.state = kListedAtI1,
		},
		{
			.start = { "R60", kListed, 2, Rows(1, 0, 1) },
			.page = kListed.page(),
			.state = kListedAtI1,
		},
		{
			.start = { "R61", kTabled, 2, Rows(1, 2, 4) },
			.page = kTabled.page(),
			.state = kTabledAtR0C0,
		},
		{
			.start = { "R62", kTabled, 2, Cells(1, 0, 3, 1, 3) },
			.page = kTabled.page(),
			.state = kTabledAtR0C0,
		},
		{
			.start = { "R63", kTabled3, 2, Cells(1, 0, 1, 1, 2) },
			.page = kTabled3.page(),
			.state = R"(2=cell:1#0,0"r0c0" ; live P0 T1 P2 nodes=12)",
		},
		{
			.start = { "R64", kFlat4, 2, PreparedEditSelection() },
			.page = kFlat4.page(),
			.state = kFlat4AtC,
		},
	});
}

[[nodiscard]] Report RemovalRejectedStaleSelection() {
	const auto first = RemovalCase{
		.start = { "R65 first", kFlat4, 2, Blocks(Root(), 3, 4) },
		.returned = 2,
		.page = Flat({ u"A"_q, u"B"_q, u"C"_q }),
		.state = R"(2=text:2"C" ; live P0 P1 P2 nodes=3)",
	};
	auto again = first;
	again.start.id = "R65";
	again.returned = std::nullopt;
	auto report = Report{ .cases = 1 };
	auto expectFirst = Expect(report.failures, first.start.id);
	auto expectAgain = Expect(report.failures, again.start.id);
	auto state = MakeState(kFlat4);
	if (Activate(expectFirst, state, kFlat4, first.start.active)) {
		CheckRemoval(expectFirst, state, first);
		CheckRemoval(expectAgain, state, again);
	}
	return report;
}

[[nodiscard]] Report RemovalRejectedClearsTemporaryParagraph() {
	return RunRemovals({
		{
			.start = { "R66", kWithTdp, 0, Blocks(Root(), 2, 5) },
			.page = kWithTdp.page(),
			.state = kTemporaryAtEmpty,
		},
	});
}

[[nodiscard]] Report SnapshotRestoreAfterRemoval() {
	const auto removal = RemovalCase{
		.start = { "R67", kQuoted, 3, Blocks(Children(1), 0, 2) },
		.returned = 1,
		.page = Framed(Quote(Paragraphs({ u"q2"_q }), u"cap"_q)),
		.state = R"(1=text:1/0"q2" ; live P0 Q1 P2 nodes=4)",
	};
	auto report = Report{ .cases = 1 };
	auto expect = Expect(report.failures, removal.start.id);
	auto state = MakeState(kQuoted);
	if (Activate(expect, state, kQuoted, removal.start.active)) {
		const auto before = Observe(state);
		CheckRemoval(expect, state, removal);
		ExpectRestored(
			expect,
			state,
			before,
			kQuoted,
			R"(3=text:1/2"q2" ; live P0 Q1 P2 nodes=6)");
	}
	return report;
}

} // namespace TestIvEditorState
