#include "extras/features/video_preview/video_preview.h"

#include "data/data_document.h"
#include "data/data_document_media.h"
#include "data/data_file_origin.h"
#include "history/history_item.h"
#include "lang_auto.h"
#include "main/main_session.h"
#include "ui/image/image_prepare.h"
#include "ui/layers/generic_box.h"
#include "ui/painter.h"
#include "ui/rp_widget.h"
#include "ui/text/format_values.h"
#include "window/window_session_controller.h"
#include "styles/style_boxes.h"
#include "styles/style_chat.h"
#include "styles/style_extras_styles.h"
#include "styles/style_layers.h"

namespace ExtrasVideoPreview {
namespace {

constexpr auto kColumns = 4;

struct Frame {
	TimeId time = 0;
	QRect source;
};

struct Storyboard {
	QImage sprite;
	QSize frameSize;
	std::vector<Frame> frames;
};

// 索引开头是 key=value 元数据，之后每行为“秒,x,y”，指向拼图里的一帧。
[[nodiscard]] std::optional<Storyboard> parseStoryboard(
		const QByteArray &map,
		QImage sprite) {
	if (sprite.isNull()) {
		return std::nullopt;
	}
	auto result = Storyboard{ .sprite = std::move(sprite) };
	auto positions = std::vector<std::pair<TimeId, QPoint>>();
	for (const auto &raw : map.split('\n')) {
		const auto line = raw.trimmed();
		if (line.startsWith("frame_width=")) {
			result.frameSize.setWidth(line.mid(12).toInt());
			continue;
		} else if (line.startsWith("frame_height=")) {
			result.frameSize.setHeight(line.mid(13).toInt());
			continue;
		}
		const auto parts = line.split(',');
		if (parts.size() != 3) {
			continue;
		}
		positions.emplace_back(
			TimeId(parts[0].toDouble()),
			QPoint(parts[1].toInt(), parts[2].toInt()));
	}
	if (result.frameSize.isEmpty()) {
		return std::nullopt;
	}
	const auto bounds = result.sprite.rect();
	for (const auto &[time, position] : positions) {
		const auto source = QRect(position, result.frameSize);
		if (bounds.contains(source)) {
			result.frames.push_back({ .time = time, .source = source });
		}
	}
	if (result.frames.empty()) {
		return std::nullopt;
	}
	return result;
}

class PreviewGrid final : public Ui::RpWidget {
public:
	PreviewGrid(QWidget *parent, Fn<void(TimeId)> chosen);

	void setStoryboard(std::optional<Storyboard> storyboard);

protected:
	int resizeGetHeight(int newWidth) override;
	void paintEvent(QPaintEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;
	void leaveEventHook(QEvent *e) override;

private:
	enum class State {
		Loading,
		Ready,
		Unavailable,
	};

	[[nodiscard]] QSize cellSize(int width) const;
	[[nodiscard]] QRect cellRect(int index) const;
	[[nodiscard]] int indexAt(QPoint point) const;
	[[nodiscard]] const QImage &frameImage(int index, QSize size);
	void paintBadge(QPainter &p, QRect cell, TimeId time) const;
	void setSelected(int index);

	const Fn<void(TimeId)> _chosen;
	State _state = State::Loading;
	Storyboard _storyboard;
	// 按当前格子大小缩放并裁好圆角的帧，格子大小变化后重建。
	std::vector<QImage> _frames;
	QSize _framesSize;
	int _selected = -1;
	int _pressed = -1;

};

PreviewGrid::PreviewGrid(QWidget *parent, Fn<void(TimeId)> chosen)
: RpWidget(parent)
, _chosen(std::move(chosen)) {
	setMouseTracking(true);
}

void PreviewGrid::setStoryboard(std::optional<Storyboard> storyboard) {
	if (!storyboard) {
		_state = State::Unavailable;
		update();
		return;
	}
	_state = State::Ready;
	_storyboard = std::move(*storyboard);
	_frames.clear();
	resizeToWidth(width());
	update();
}

QSize PreviewGrid::cellSize(int width) const {
	const auto skip = st::extrasVideoPreviewSkip;
	const auto cellWidth = (width - skip * (kColumns - 1)) / kColumns;
	const auto frame = _storyboard.frameSize;
	return QSize(cellWidth, cellWidth * frame.height() / frame.width());
}

int PreviewGrid::resizeGetHeight(int newWidth) {
	if (_state != State::Ready) {
		return st::extrasVideoPreviewPlaceholderHeight;
	}
	const auto count = int(_storyboard.frames.size());
	const auto rows = (count + kColumns - 1) / kColumns;
	const auto skip = st::extrasVideoPreviewSkip;
	return rows * cellSize(newWidth).height() + (rows - 1) * skip;
}

QRect PreviewGrid::cellRect(int index) const {
	const auto size = cellSize(width());
	const auto skip = st::extrasVideoPreviewSkip;
	return QRect(
		QPoint(
			(index % kColumns) * (size.width() + skip),
			(index / kColumns) * (size.height() + skip)),
		size);
}

int PreviewGrid::indexAt(QPoint point) const {
	if (_state != State::Ready) {
		return -1;
	}
	const auto count = int(_storyboard.frames.size());
	for (auto i = 0; i != count; ++i) {
		if (cellRect(i).contains(point)) {
			return i;
		}
	}
	return -1;
}

const QImage &PreviewGrid::frameImage(int index, QSize size) {
	if (_framesSize != size) {
		_framesSize = size;
		_frames.clear();
	}
	if (_frames.empty()) {
		_frames.resize(_storyboard.frames.size());
	}
	auto &result = _frames[index];
	if (result.isNull()) {
		const auto ratio = style::DevicePixelRatio();
		result = _storyboard.sprite.copy(
			_storyboard.frames[index].source
		).scaled(
			size * ratio,
			Qt::IgnoreAspectRatio,
			Qt::SmoothTransformation);
		result.setDevicePixelRatio(ratio);
		result = Images::Round(std::move(result), ImageRoundRadius::Small);
	}
	return result;
}

void PreviewGrid::paintBadge(QPainter &p, QRect cell, TimeId time) const {
	const auto text = Ui::FormatDurationText(time);
	const auto &font = st::msgDateFont;
	const auto padding = st::msgDateImgPadding;
	const auto margin = st::extrasVideoPreviewBadgeMargin;
	const auto size = QSize(
		font->width(text) + 2 * padding.x(),
		font->height + 2 * padding.y());
	const auto badge = QRect(
		cell.x() + margin,
		cell.y() + cell.height() - margin - size.height(),
		size.width(),
		size.height());
	{
		auto hq = PainterHighQualityEnabler(p);
		const auto radius = badge.height() / 2.;
		p.setPen(Qt::NoPen);
		p.setBrush(st::msgDateImgBg);
		p.drawRoundedRect(badge, radius, radius);
	}
	p.setFont(font);
	p.setPen(st::msgDateImgFg);
	p.drawText(
		badge.x() + padding.x(),
		badge.y() + padding.y() + font->ascent,
		text);
}

void PreviewGrid::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);
	if (_state != State::Ready) {
		p.setFont(st::boxTextFont);
		p.setPen(st::windowSubTextFg);
		p.drawText(
			rect(),
			((_state == State::Loading)
				? tr::extras_VideoPreviewLoading(tr::now)
				: tr::extras_VideoPreviewUnavailable(tr::now)),
			style::al_center);
		return;
	}
	const auto clip = e->rect();
	const auto size = cellSize(width());
	const auto count = int(_storyboard.frames.size());
	for (auto i = 0; i != count; ++i) {
		const auto cell = cellRect(i);
		if (!cell.intersects(clip)) {
			continue;
		}
		p.drawImage(cell.topLeft(), frameImage(i, size));
		paintBadge(p, cell, _storyboard.frames[i].time);
		if (i != _selected) {
			continue;
		}
		auto hq = PainterHighQualityEnabler(p);
		const auto line = st::extrasVideoPreviewBorder;
		const auto half = line / 2.;
		const auto radius = st::roundRadiusSmall - half;
		p.setPen(QPen(st::activeLineFg, line));
		p.setBrush(Qt::NoBrush);
		p.drawRoundedRect(
			QRectF(cell).marginsRemoved({ half, half, half, half }),
			radius,
			radius);
	}
}

void PreviewGrid::setSelected(int index) {
	if (_selected == index) {
		return;
	}
	if (_selected >= 0) {
		update(cellRect(_selected));
	}
	_selected = index;
	if (_selected >= 0) {
		update(cellRect(_selected));
	}
	setCursor((_selected >= 0) ? style::cur_pointer : style::cur_default);
}

void PreviewGrid::mouseMoveEvent(QMouseEvent *e) {
	setSelected(indexAt(e->pos()));
}

void PreviewGrid::mousePressEvent(QMouseEvent *e) {
	if (e->button() == Qt::LeftButton) {
		_pressed = indexAt(e->pos());
	}
}

void PreviewGrid::mouseReleaseEvent(QMouseEvent *e) {
	const auto pressed = std::exchange(_pressed, -1);
	if (e->button() != Qt::LeftButton
		|| pressed < 0
		|| pressed != indexAt(e->pos())) {
		return;
	}
	// 回调会同步关闭弹窗并销毁本控件和 _chosen，先复制到栈上再调用。
	const auto onstack = _chosen;
	onstack(_storyboard.frames[pressed].time);
}

void PreviewGrid::leaveEventHook(QEvent *e) {
	setSelected(-1);
}

void fillBox(
		not_null<Ui::GenericBox*> box,
		not_null<Window::SessionController*> controller,
		not_null<HistoryItem*> item,
		not_null<DocumentData*> document) {
	Expects(hasPreview(document));

	const auto context = Window::SessionController::MessageContext{
		.id = item->fullId(),
		.topicRootId = item->topicRootId(),
		.monoforumPeerId = item->sublistPeerId(),
	};
	box->setTitle(tr::extras_VideoPreviewTitle());
	box->setWidth(st::boxWideWidth);
	box->setMaxHeight(st::extrasVideoPreviewMaxHeight);
	box->addButton(tr::lng_close(), [=] { box->closeBox(); });

	const auto grid = box->addRow(object_ptr<PreviewGrid>(
		box,
		[=](TimeId time) {
			box->closeBox();
			controller->openDocument(document, true, context, nullptr, time);
		}));

	const auto data = document->video();
	const auto origin = Data::FileOrigin(context.id);
	const auto sprite = data->storyboard->createMediaView();
	const auto map = data->storyboardMap->createMediaView();
	for (const auto &media : { sprite, map }) {
		const auto file = media->owner();
		// 拼图与索引只有几十 KB，存进本地缓存，再次打开不必重新下载。
		file->forceToCache(true);
		file->save(origin, QString());
	}
	rpl::single() | rpl::then(
		document->session().downloaderTaskFinished()
	) | rpl::filter([=] {
		return sprite->loaded() && map->loaded();
	}) | rpl::take(1) | rpl::on_next([=] {
		grid->setStoryboard(parseStoryboard(
			map->bytes(),
			Images::Read({ .content = sprite->bytes() }).image));
	}, grid->lifetime());
}

} // namespace

bool hasPreview(not_null<DocumentData*> document) {
	const auto data = document->video();
	return data && data->storyboard && data->storyboardMap;
}

void showBox(
		not_null<Window::SessionController*> controller,
		not_null<HistoryItem*> item,
		not_null<DocumentData*> document) {
	controller->show(Box(fillBox, controller, item, document));
}

} // namespace ExtrasVideoPreview
