/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "iv/editor/iv_editor_page_table_grid.h"
#include "iv/editor/iv_editor_state.h"

#include <QtCore/QStringList>

#include <initializer_list>
#include <optional>
#include <vector>

namespace TestIvEditorState {

using namespace Iv::Editor;
using Iv::RichMessageLimitError;
using Iv::RichMessageLimits;
using Iv::RichPage;
using Iv::Markdown::PreparedBlock;
using Iv::Markdown::PreparedBlockKind;
using Iv::Markdown::PreparedEditBlockContainerKind;
using Iv::Markdown::PreparedEditBlockContainerPath;
using Iv::Markdown::PreparedEditBlockDropTarget;
using Iv::Markdown::PreparedEditDropTarget;
using Iv::Markdown::PreparedEditListItemDropTarget;
using Iv::Markdown::PreparedEditSelection;
using Iv::Markdown::PreparedEditSelectionKind;
using Iv::Markdown::PreparedEditTableCellRange;
using Iv::Markdown::PreparedEditTextDropTarget;
using Iv::Markdown::PrepareTableRenderLimitsForRichMessage;

using Action = State::BoundaryTarget::Action;
using Block = RichPage::Block;
using BlockKind = RichPage::BlockKind;
using ListKind = RichPage::ListKind;
using Result = State::ApplyResult;

inline constexpr auto kFlat4AtC = R"(2=text:2"C" ; live P0 P1 P2 P3 nodes=4)";
inline constexpr auto kListedAtI1 = R"(2=item:1#1"i1" ; live P0 L1 P2 nodes=5)";
inline constexpr auto kTabledAtR0C0 = R"(2=cell:1#0,0"r0c0" ; live P0 T1 P2 nodes=9)";
inline constexpr auto kListed4AtI1 = R"(2=item:1#1"i1" ; live P0 L1 P2 nodes=6)";
inline constexpr auto kTwoListsAtA0 = R"(0=item:0#0"a0" ; live L0 P1 L2 nodes=5)";
inline constexpr auto kTemporaryAtEmpty = R"(1=text:1"" ; live P0 P1 P2 nodes=3)";

struct Cell {
	QString text;
	int colspan = 1;
	int rowspan = 1;
};

using CellRows = std::vector<std::vector<Cell>>;

struct Fixture {
	const char *name = nullptr;
	RichPage (*page)() = nullptr;
	RichMessageLimits limits;
	bool temporaryParagraph = false;
	const char *text = nullptr;
	const char *state = nullptr;
	const char *grid = nullptr;
};

struct Start {
	const char *id = nullptr;
	Fixture fixture;
	int active = 0;
	PreparedEditSelection selection;
};

struct RemovalCase {
	Start start;
	bool forward = true;
	std::optional<int> returned;
	RichPage page;
	const char *state = nullptr;
	const char *leaves = nullptr;
	const char *spans = nullptr;
	const char *grid = nullptr;
};

struct Outcome {
	Result result = Result::Failed;
	State::BoundaryTarget destination;
};

struct DropCase {
	Start start;
	PreparedEditDropTarget target;
	Outcome outcome;
	std::optional<RichMessageLimitError> limit;
	RichPage page;
	const char *state = nullptr;
	const char *leaves = nullptr;
	bool temporary = false;
};

struct Observed {
	State::Snapshot snapshot;
	const RichPage *page = nullptr;
	int ordinal = -1;
	TextWithEntities text;
	QString summary;
	QString leaves;
};

struct Report {
	int cases = 0;
	QStringList failures;
};

struct Check {
	const char *name = nullptr;
	Report (*run)() = nullptr;
};

class Expect final {
public:
	Expect(QStringList &failures, const char *id);

	void same(
		const char *label,
		bool equal,
		QString got,
		const QString &expected);
	void text(const char *label, const QString &got, const QString &expected);
	void number(const char *label, int got, int expected);
	void page(const char *label, const RichPage &got, const RichPage &expected);

private:
	QStringList &_failures;
	QString _id;

};

extern const Fixture kFlat4;
extern const Fixture kFlat5;
extern const Fixture kQuoted;
extern const Fixture kListed;
extern const Fixture kListed4;
extern const Fixture kNested;
extern const Fixture kTabled;
extern const Fixture kTabled3;
extern const Fixture kUntitled;
extern const Fixture kColMerged;
extern const Fixture kRowMerged;
extern const Fixture kRowSpanRows;
extern const Fixture kRowSpanThree;
extern const Fixture kEmptyTarget;
extern const Fixture kTrailingSpans;
extern const Fixture kSpanGap;
extern const Fixture kUnorderedTarget;
extern const Fixture kLongSpan;
extern const Fixture kUnplacedSpan;
extern const Fixture kOmittedSpan;
extern const Fixture kOmittedSpanOneColumn;
extern const Fixture kTwoLists;
extern const Fixture kListFirst;
extern const Fixture kMixedLists;
extern const Fixture kWithU;
extern const Fixture kQuoteDepth;
extern const Fixture kEmptyQuote;
extern const Fixture kWithTdp;
extern const Fixture kWithDivider;
extern const Fixture kFlat4ThreeBlocks;

[[nodiscard]] Block Paragraph(const QString &text);
[[nodiscard]] std::vector<Block> Paragraphs(
		std::initializer_list<QString> texts);
[[nodiscard]] RichPage::ListItem Item(
		const QString &text,
		std::vector<Block> children = {});
[[nodiscard]] Block List(
		std::vector<RichPage::ListItem> items,
		ListKind kind = ListKind::Bullet,
		std::optional<int> start = std::nullopt);
[[nodiscard]] Block List(std::initializer_list<QString> texts);
[[nodiscard]] Block Quote(
		std::vector<Block> children,
		const QString &caption,
		const QString &text = QString());
[[nodiscard]] Block Table(const QString &title, const CellRows &rows);
[[nodiscard]] CellRows Grid(
		std::initializer_list<int> rows,
		std::initializer_list<int> columns);
[[nodiscard]] RichPage Page(std::vector<Block> blocks);
[[nodiscard]] RichPage Flat(std::initializer_list<QString> texts);
[[nodiscard]] RichPage Framed(Block middle);
[[nodiscard]] RichPage Framed(const QString &title, const CellRows &rows);
[[nodiscard]] RichPage WithDecoratedCell(RichPage page, int row, int cell);

[[nodiscard]] PreparedEditBlockContainerPath Root();
[[nodiscard]] PreparedEditBlockContainerPath Children(int block);
[[nodiscard]] PreparedEditBlockContainerPath ItemChildren(int block, int item);
[[nodiscard]] PreparedEditSelection Blocks(
		PreparedEditBlockContainerPath container,
		int from,
		int till);
[[nodiscard]] PreparedEditSelection Items(int block, int from, int till);
[[nodiscard]] PreparedEditSelection Rows(int block, int from, int till);
[[nodiscard]] PreparedEditSelection Cells(
		int block,
		int rowFrom,
		int rowTill,
		int columnFrom,
		int columnTill);

[[nodiscard]] State MakeState(const Fixture &fixture);
[[nodiscard]] bool Activate(
		Expect &expect,
		State &state,
		const Fixture &fixture,
		int active);
[[nodiscard]] Observed Observe(const State &state);
void CheckRemoval(Expect &expect, State &state, const RemovalCase &test);
void CheckMenuRemoval(Expect &expect, State &state, const RemovalCase &test);
void ExpectRestored(
		Expect &expect,
		State &state,
		const Observed &before,
		const Fixture &fixture,
		const char *summary);
[[nodiscard]] Report RunRemovals(const std::vector<RemovalCase> &cases);

template <typename Case>
[[nodiscard]] Report RunCases(
		const std::vector<Case> &cases,
		void (*check)(Expect &, State &, const Case &)) {
	auto report = Report();
	for (const auto &test : cases) {
		const auto &start = test.start;
		auto expect = Expect(report.failures, start.id);
		auto state = MakeState(start.fixture);
		++report.cases;
		if (Activate(expect, state, start.fixture, start.active)) {
			check(expect, state, test);
		}
	}
	return report;
}

[[nodiscard]] Report RemovalBlocksRangeAndDirection();
[[nodiscard]] Report RemovalBlocksActiveLeafKept();
[[nodiscard]] Report RemovalBlocksActiveLeafRemoved();
[[nodiscard]] Report RemovalBlocksLeadingPlannedFocus();
[[nodiscard]] Report RemovalQuoteChildren();
[[nodiscard]] Report RemovalListItems();
[[nodiscard]] Report RemovalListItemsLeadingPlannedFocus();
[[nodiscard]] Report RemovalListItemChildren();
[[nodiscard]] Report RemovalTableRows();
[[nodiscard]] Report RemovalTableRowsMerged();
[[nodiscard]] Report RemovalTableRowsMenu();
[[nodiscard]] Report RemovalTableColumns();
[[nodiscard]] Report RemovalTableColumnsWhole();
[[nodiscard]] Report RemovalTableColumnsMerged();
[[nodiscard]] Report RemovalRejected();
[[nodiscard]] Report RemovalRejectedStaleSelection();
[[nodiscard]] Report RemovalRejectedClearsTemporaryParagraph();
[[nodiscard]] Report SnapshotRestoreAfterRemoval();

} // namespace TestIvEditorState
