/* GemRB - Infinity Engine Emulator
 * Copyright (C) 2003 The GemRB Project
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 *
 */

#include "TextArea.h"

#include "Interface.h"
#include "Variables.h"
#include "GUI/EventMgr.h"
#include "GUI/ScrollBar.h"
#include "GUI/Window.h"

namespace GemRB {
	
TextArea::SpanSelector::SpanSelector(TextArea& ta, const std::vector<const String*>& opts, bool numbered, Margin m)
: ContentContainer(Region(0, 0, ta.Frame().w, 0)), ta(ta)
{
	SetFlags(RESIZE_WIDTH, OP_NAND);

	selectedSpan = NULL;
	hoverSpan = NULL;

	size = opts.size();

	SetMargin(m);

	Size flexFrame(-1, 0); // flex frame for hanging indent after optnum
	String format = L". - ";
	int numWidth = int(ta.ftext->StringSizeWidth(std::to_wstring(opts.size()) + format, 0)) + 3; // good guess at max width
	Size numFrame(numWidth, ta.ftext->LineHeight); // size for the numerical prefix so they stay aligned
	Point origin(margin.left, margin.top);
	Region r(origin, Dimensions());
	r.w = std::max(r.w - margin.left - margin.right, 0);
	r.h = std::max(r.h - margin.top - margin.bottom, 0);
	
	Font::PrintColors colors {ta.colors[COLOR_OPTIONS], ta.colors[COLOR_BACKGROUND]};
	Font::PrintColors selectedCol {ta.colors[COLOR_SELECTED], ta.colors[COLOR_BACKGROUND]};

	for (size_t i = 0; i < opts.size(); i++) {
		TextContainer* selOption = new OptSpan(r, ta.ftext, colors.fg, colors.bg);
		selOption->SetAutoResizeFlags(ResizeHorizontal, OP_SET);

		if (numbered) {
			// TODO: as per the original PALETTE_SELECTED should be updated to the PC color (same color their name is rendered in)
			// but that should probably actually be done by the dialog handler, not here.
			auto ts = new TextSpan(std::to_wstring(i + 1) + format, nullptr, selectedCol, &numFrame);
			ts->Alignment = IE_FONT_ALIGN_RIGHT;
			selOption->AppendContent(ts);
		}
		selOption->AppendContent(new TextSpan(*opts[i], nullptr, &flexFrame));
		AddSubviewInFrontOfView(selOption);

		if (EventMgr::TouchInputEnabled) {
			// keeping the options spaced out (for touch screens)
			r.y += ta.LineHeight();
		}
		r.y += selOption->Dimensions().h;
	}
	
	SetFrameSize(Size(r.w, r.y)); // r.y is not a typo, its the location where the next option would have been

	if (numbered) {
		// in a sane world we would simply focus the window and this View
		// unfortunately, focusing the window makes it overlap with the portwin/optwin...
		EventMgr::EventCallback cb = METHOD_CALLBACK( &SpanSelector::KeyEvent, this);
		id = EventMgr::RegisterEventMonitor(cb, Event::KeyDownMask);
	} else {
		id = -1;
	}

	assert((Flags()&RESIZE_WIDTH) == 0);
}
	
TextArea::SpanSelector::~SpanSelector()
{
	EventMgr::UnRegisterEventMonitor(id);
}

void TextArea::SpanSelector::SizeChanged(const Size&)
{
	// NOTE: this wouldnt be needed if we used TextSpans (layout) for the options, but then we would have to
	// write more complex code for the hover effects and selection
	Point origin(margin.left, margin.top);
	Region r(origin, Size(frame.w, 0));
	r.w = std::max(r.w - margin.left - margin.right, 0);
	r.h = std::max(r.h - margin.top - margin.bottom, 0);

	for (auto it = subViews.rbegin(); it != subViews.rend(); ++it) {
		View* selOption = *it;

		selOption->SetFrame(r);

		if (EventMgr::TouchInputEnabled) {
			// keeping the options spaced out (for touch screens)
			r.y += ta.LineHeight();
		}
		r.y += selOption->Dimensions().h;
	}
	
	frame.h = std::max(frame.h, r.y + margin.bottom);
}

bool TextArea::SpanSelector::KeyEvent(const Event& event)
{
	return OnKeyPress(event.keyboard, 0);
}

bool TextArea::SpanSelector::OnKeyPress(const KeyboardEvent& key, unsigned short /*mod*/)
{
	KeyboardKey chr = key.character;
	if (chr < '1' || chr > '9')
		return false;

	unsigned int idx = chr - '1';
	MakeSelection(idx);
	return true;
}

void TextArea::SpanSelector::ClearHover()
{
	if (hoverSpan) {
		if (hoverSpan == selectedSpan) {
			hoverSpan->SetColors(ta.colors[COLOR_SELECTED], ta.colors[COLOR_BACKGROUND]);
		} else {
			// reset the old hover span
			hoverSpan->SetColors(ta.colors[COLOR_OPTIONS], ta.colors[COLOR_BACKGROUND]);
		}
		hoverSpan = NULL;
	}
}

void TextArea::SpanSelector::MakeSelection(size_t idx)
{
	TextContainer* optspan = TextAtIndex(idx);

	if (optspan == selectedSpan) {
		return; // already selected
	}

	if (selectedSpan && selectedSpan != optspan) {
		// reset the previous selection
		selectedSpan->SetColors(ta.colors[COLOR_OPTIONS], ta.colors[COLOR_BACKGROUND]);
	}
	selectedSpan = optspan;
	
	if (selectedSpan) {
		selectedSpan->SetColors(ta.colors[COLOR_SELECTED], ta.colors[COLOR_BACKGROUND]);
	}

	// beware, this will recursively call this function.
	ta.UpdateState(static_cast<unsigned int>(idx));
}
	
TextContainer* TextArea::SpanSelector::TextAtPoint(const Point& p)
{
	// container only has text, so...
	return static_cast<TextContainer*>(SubviewAt(p, true, false));
}
	
TextContainer* TextArea::SpanSelector::TextAtIndex(size_t idx)
{
	if (subViews.empty() || idx > subViews.size() - 1) {
		return NULL;
	}

	std::list<View*>::reverse_iterator it = subViews.rbegin();
	std::advance(it, idx);
	return static_cast<TextContainer*>(*it);
}

bool TextArea::SpanSelector::OnMouseOver(const MouseEvent& me)
{
	Point p = ConvertPointFromScreen(me.Pos());
	TextContainer* span = TextAtPoint(p);
	
	if (hoverSpan || span)
		MarkDirty();
	
	ClearHover();
	if (span) {
		hoverSpan = span;
		hoverSpan->SetColors(ta.colors[COLOR_HOVER], ta.colors[COLOR_BACKGROUND]);
	}
	return true;
}
	
bool TextArea::SpanSelector::OnMouseUp(const MouseEvent& me, unsigned short /*Mod*/)
{
	Point p = ConvertPointFromScreen(me.Pos());
	TextContainer* span = TextAtPoint(p);
	
	if (span) {
		std::list<View*>::reverse_iterator it = subViews.rbegin();
		unsigned int idx = 0;
		while (*it++ != span) { ++idx; };
		
		MakeSelection(idx);
	}
	return true;
}
	
void TextArea::SpanSelector::OnMouseLeave(const MouseEvent& me, const DragOp* op)
{
	ClearHover();
	ContentContainer::OnMouseLeave(me, op);
}

TextArea::TextArea(const Region& frame, Font* text)
: TextArea(frame, text, text, ColorWhite, ColorWhite, ColorBlack)
{}

TextArea::TextArea(const Region& frame, Font* text, Font* caps,
				   const Color& textcolor, const Color& initcolor, const Color& textBgColor)
: Control(frame), scrollview(Region(Point(), Dimensions())), ftext(text), colors()
{
	colors[COLOR_NORMAL] = textcolor;
	colors[COLOR_INITIALS] = initcolor;
	colors[COLOR_BACKGROUND] = textBgColor;

	// quick font optimization (prevents creating unnecessary cap spans)
	finit = (caps && caps != ftext) ? caps : ftext;
	assert(ftext && finit);

	parser.ResetAttributes(text, {textcolor, textBgColor}, finit, {initcolor, textBgColor});

	ControlType = IE_GUI_TEXTAREA;
	strncpy(VarName, "Selected", sizeof(VarName));

	selectOptions = NULL;
	textContainer = NULL;
	historyTimer = NULL;
	
	AddSubviewInFrontOfView(&scrollview);

	// initialize the Text containers
	ClearSelectOptions();
	ClearText();
	SetAnimPicture(NULL);

	scrollview.SetScrollIncrement(LineHeight());
	scrollview.SetAutoResizeFlags(ResizeAll, OP_SET);
	scrollview.SetFlags(View::IgnoreEvents, (Flags()&View::IgnoreEvents) ? OP_OR : OP_NAND);
}

TextArea::~TextArea()
{
	ClearHistoryTimer();
}

void TextArea::DrawSelf(Region drawFrame, const Region& /*clip*/)
{
	if (AnimPicture) {
		// speaker portrait
		core->GetVideoDriver()->BlitSprite(AnimPicture, drawFrame.Origin());
	}
}

void TextArea::SetAnimPicture(Holder<Sprite2D> pic)
{
	if (core->HasFeature(GF_ANIMATED_DIALOG)) {
		// FIXME: there isnt a specific reason why animatied dialog couldnt also use pics
		// However, PST does not and the animation makes the picture spaz currently
		return;
	}

	Control::SetAnimPicture(pic);

	assert(textContainer);
	UpdateTextFrame();
}

ContentContainer::Margin TextArea::GetMargins() const
{
	return textMargins;
}

void TextArea::SetMargins(ContentContainer::Margin m)
{
	textMargins = m;
	if (textContainer)
		textContainer->SetMargin(textMargins);
}

ieDword TextArea::LineCount() const
{
	int rowHeight = LineHeight();
	if (rowHeight > 0)
		return (ContentHeight() + rowHeight - 1) / rowHeight; // round up
	else
		return 0;
}

Region TextArea::UpdateTextFrame()
{
	const Region& cr = scrollview.ContentRegion();
	if (textContainer) {
		Region r = textContainer->Frame();
		r.w = cr.w + cr.x;
		r.h = 0; // auto grow

		if (AnimPicture) {
			// shrink and shift the container to accommodate the image
			r.x = AnimPicture->Frame.w + 5;
			r.w -= r.x;
		} else {
			r.x = 0;
		}

		textContainer->SetFrame(r);
		scrollview.Update();
		return textContainer->Frame();
	}
	return Region(Point(0,0), Size(cr.w + cr.x, 0));
}

void TextArea::UpdateScrollview()
{
	if (Flags()&AutoScroll
		&& dialogBeginNode) {
		assert(textContainer && selectOptions);
		
		Region textFrame = UpdateTextFrame();
		textFrame.y = textFrame.h;
		textFrame.h = selectOptions->Frame().h;
		selectOptions->SetFrame(textFrame);

		Region nodeBounds = textContainer->BoundingBoxForContent(dialogBeginNode);
		int optH = OptionsHeight();
		ieDword anim = 0;
		int y = 0;

		if (core->HasFeature(GF_ANIMATED_DIALOG)) {
			anim = 500;
			y = -9999999; // FIXME: properly calculate the "bottom"?
		} else {
			int blankH = frame.h - LineHeight() - nodeBounds.h - optH;
			if (blankH > 0) {
				optH += blankH;
				int width = selectOptions->Frame().w;
				selectOptions->SetFrameSize(Size(width, optH));
			}

			// now scroll dialogBeginNode to the top less a blank line
			y = nodeBounds.y - LineHeight();
		}

		// FIXME: must update before the scroll, but this should be automaticly done as a reaction to changing sizes/origins of subviews
		scrollview.Update();
		scrollview.ScrollTo(Point(0, -y), anim);
	} else if (!core->HasFeature(GF_ANIMATED_DIALOG)) {
		scrollview.Update();
	}
	
	Region textFrame = UpdateTextFrame();
	if (selectOptions) {
		textFrame.y = textFrame.h;
		textFrame.h = selectOptions->Frame().h;
		selectOptions->SetFrame(textFrame);
	}
}

void TextArea::FlagsChanged(unsigned int oldflags)
{
	if (Flags()&View::IgnoreEvents) {
		scrollview.SetFlags(View::IgnoreEvents, OP_OR);
	} else if (oldflags&View::IgnoreEvents) {
		scrollview.SetFlags(View::IgnoreEvents, OP_NAND);
	}

	if (Flags()&Editable) {
		assert(textContainer);
		textContainer->SetFlags(View::IgnoreEvents, OP_NAND);
		textContainer->SetEventProxy(NULL);
		SetEventProxy(textContainer);
	} else if (oldflags&Editable) {
		assert(textContainer);
		textContainer->SetFlags(View::IgnoreEvents, OP_OR);
		textContainer->SetEventProxy(&scrollview);
		SetEventProxy(&scrollview);
	}
}

/** Sets the Actual Text */
void TextArea::SetText(const String& text)
{
	ClearText();
	AppendText(text);
}

void TextArea::SetColor(const Color& color, COLOR_TYPE idx)
{
	assert(idx < COLOR_TYPE_COUNT);
	colors[idx] = color;
	parser.ResetAttributes(ftext, {colors[COLOR_NORMAL], colors[COLOR_BACKGROUND]}, finit, {colors[COLOR_INITIALS], colors[COLOR_BACKGROUND]});
}

void TextArea::SetColor(const Color* color, COLOR_TYPE idx)
{
	if (color) {
		SetColor(*color, idx);
	} else {
		SetColor(colors[COLOR_NORMAL], idx);
	}
}

void TextArea::ClearHistoryTimer()
{
	if (historyTimer) {
		historyTimer->Invalidate();
		historyTimer = nullptr;
	}
}

void TextArea::TrimHistory(size_t lines)
{
	if (dialogBeginNode) {
		// we don't trim history in dialog
		// this allows us to always reference the entire dialog no matter how long it is
		// we would also have to reapply the selection options origin since it will often be changed by trimming
		// e.g. selectOptions->SetFrameOrigin(Point(textFrame.x, textFrame.h));
		return;
	}

	int height = int(LineHeight() * lines);
	Region exclusion(Point(), Size(frame.w, height));
	scrollview.ScrollDelta(Point(0, exclusion.h));
	textContainer->DeleteContentsInRect(exclusion);
	scrollview.Update();

	ClearHistoryTimer();
}

void TextArea::AppendText(const String& text)
{
	if ((flags&ClearHistory)) {
		ClearHistoryTimer();

		int heightLimit = (ftext->LineHeight * 100); // 100 lines of content
		int currHeight = ContentHeight();
		if (currHeight > heightLimit) {
			size_t lines = (currHeight - heightLimit) / LineHeight();

			EventHandler h = [this, lines]() {
				TrimHistory(lines);
			};
			assert(historyTimer == NULL);
			historyTimer = &core->SetTimer(h, 500);
		}
	}

	size_t tagPos = text.find_first_of('[');
	if (tagPos != String::npos) {
		parser.ParseMarkupStringIntoContainer(text, *textContainer);
	} else if (text.length()) {
		if (finit != ftext) {
			// append cap spans
			size_t textpos = text.find_first_not_of(WHITESPACE_STRING);
			if (textpos != String::npos) {
				// first append the white space as its own span
				textContainer->AppendText(text.substr(0, textpos));

				// we must create and append this span here (instead of using AppendText),
				// because the original data files for the DC font specifies a line height of 13
				// that would cause overlap when the lines wrap beneath the DC if we didnt specify the correct size
				Size s = finit->GetGlyph(text[textpos]).size;
				if (s.h > ftext->LineHeight) {
					// pad this only if it is "real" (it is higher than the other text).
					// some text areas have a "cap" font assigned in the CHU that differs from ftext, but isnt meant to be a cap
					// see BG2 chargen
					s.w += 3;
				}
				TextSpan* dc = new TextSpan(text.substr(textpos, 1), finit, {ColorWhite, ColorBlack}, &s);
				textContainer->AppendContent(dc);
				textpos++;
				// FIXME: assuming we have more text!
				// FIXME: as this is currently implemented, the cap is *not* considered part of the word,
				// there is potential wrapping errors (BG2 char gen).
				// we could solve this by wrapping the cap and the letters remaining letters of the word into their own TextContainer
			} else {
				textpos = 0;
			}
			textContainer->AppendText(text.substr(textpos));
		} else {
			textContainer->AppendText(text);
		}
	}

	UpdateScrollview();

	if (flags&AutoScroll && !selectOptions)
	{
		// scroll to the bottom
		int bottom = ContentHeight() - frame.h;
		if (bottom > 0)
			ScrollToY(-bottom, 500);
	}
	MarkDirty();
}
/*
int TextArea::InsertText(const char* text, int pos)
{
	// TODO: actually implement this
	AppendText(text);
	return pos;
}
*/

ieWord TextArea::LineHeight() const
{
	return ftext->LineHeight;
}

void TextArea::ScrollDelta(const Point& p)
{
	scrollview.ScrollTo(p);
}

void TextArea::ScrollTo(const Point& p)
{
	scrollview.ScrollTo(p);
}

/** Will scroll y pixels over duration */
void TextArea::ScrollToY(int y, ieDword duration)
{
	scrollview.ScrollTo(Point(0, y), duration);
}

void TextArea::UpdateState(unsigned int optIdx)
{
	if (!selectOptions) {
		// no selectable options present
		// set state to safe and return
		ClearSelectOptions();
		return;
	}

	if (!VarName[0]) {
		return;
	}
	
	if (optIdx >= selectOptions->NumOpts()) {
		SetValue(-1);
		selectOptions->MakeSelection(-1);
		return;
	}

	assert(optIdx < values.size());
	// always run the TextAreaOnSelect handler even if the value hasnt changed
	// the *context* of the value can change (dialog) and the handler will want to know 
	SetValue( values[optIdx] );

	// this can be called from elsewhere (GUIScript), so we need to make sure we update the selected span
	selectOptions->MakeSelection(optIdx);

	PerformAction(Action::Select);
}

void TextArea::DidFocus()
{
	if (Flags()&Editable) {
		textContainer->DidFocus();
	}
}

void TextArea::DidUnFocus()
{
	if (Flags()&Editable) {
		textContainer->DidUnFocus();
	}
}

void TextArea::AddSubviewInFrontOfView(View* front, const View* back)
{
	// we dont have a way of retrieving a TextArea's scrollview so
	// we have no direct way of placing subviews in front of it so we let NULL represent it
	const View* target = (back) ? back : &scrollview;
	View::AddSubviewInFrontOfView(front, target);
}
	
int TextArea::TextHeight() const
{
	return (textContainer) ? textContainer->Dimensions().h : 0;
}
int TextArea::OptionsHeight() const
{
	return (selectOptions) ? selectOptions->Dimensions().h : 0;
}

int TextArea::ContentHeight() const
{
	return TextHeight() + OptionsHeight();
}

String TextArea::QueryText() const
{
	if (selectOptions) {
		if (selectOptions->Selection()) {
			return selectOptions->Selection()->Text();
		} else {
			Log(ERROR, "TextArea", "QueryText: No selection found!");
			return String();
		}
	}
	if (textContainer) {
		return textContainer->Text();
	}
	return String();
}

void TextArea::ClearSelectOptions()
{
	values.clear();
	delete scrollview.RemoveSubview(selectOptions);
	dialogBeginNode = NULL;
	selectOptions = NULL;

	if (!core->HasFeature(GF_ANIMATED_DIALOG)) {
		UpdateScrollview();
	}
}

void TextArea::SetScrollbar(ScrollBar* sb)
{
	const Region& sbr = sb->Frame();
	const Region& tar = Frame();
	
	ContentContainer::Margin margins = GetMargins();
	
	Region combined = Region::RegionEnclosingRegions(sbr, tar);
	margins.top += tar.y - combined.y;
	margins.left += tar.x - combined.x;
	margins.right += (combined.x + combined.w) - (tar.x + tar.w);
	margins.bottom += (combined.y + combined.h) - (tar.y + tar.h);

	constexpr uint8_t MINIMUM_H_MARGIN = 3;
	margins.right = std::max(margins.right, MINIMUM_H_MARGIN);
	margins.left = std::max(margins.left, MINIMUM_H_MARGIN);

	SetFrame(combined);
	SetMargins(margins);
	
	Point origin = ConvertPointFromWindow(sb->Frame().Origin());
	sb->SetFrameOrigin(origin);

	scrollview.SetVScroll(sb);
}

void TextArea::SetSelectOptions(const std::vector<SelectOption>& opts, bool numbered,
								const Color* color, const Color* hiColor, const Color* selColor)
{
	SetColor(color, COLOR_OPTIONS);
	SetColor(hiColor, COLOR_HOVER);
	SetColor(selColor, COLOR_SELECTED);

	ClearSelectOptions(); // deletes previous options

	ContentContainer::ContentList::const_reverse_iterator it = textContainer->Contents().rbegin();
	if (it != textContainer->Contents().rend()) {
		dialogBeginNode = *it; // need to get the last node *before* we append anything
	}

	values.resize(opts.size());
	std::vector<const String*> strings(opts.size());
	for (size_t i = 0; i < opts.size(); i++) {
		values[i] = opts[i].first;
		strings[i] = &(opts[i].second);
	}

	ContentContainer::Margin m;
	size_t selectIdx = -1;
	if (dialogBeginNode) {
		if (AnimPicture)
			m = ContentContainer::Margin(10, 20);
		else
			m = ContentContainer::Margin(LineHeight(), 40, 10);
	} else if (LineCount() > 0) {
		m = ContentContainer::Margin(0, 3);
		selectIdx = GetValue();
	} else {
		m = textMargins;
	}

	selectOptions = new SpanSelector(*this, strings, numbered, m);
	scrollview.AddSubviewInFrontOfView(selectOptions);
	selectOptions->MakeSelection(selectIdx);

	UpdateScrollview();
}

void TextArea::SelectAvailableOption(size_t idx)
{
	if (selectOptions) {
		selectOptions->MakeSelection(idx);
	}
}

void TextArea::TextChanged(TextContainer& /*tc*/)
{
	PerformAction(Action::Change);
}

void TextArea::ClearText()
{
	delete scrollview.RemoveSubview(textContainer);

	parser.Reset(); // reset in case any tags were left open from before
	textContainer = new TextContainer(Region(Point(), Dimensions()), ftext);
	textContainer->SetColors(colors[COLOR_NORMAL], colors[COLOR_BACKGROUND]);
	textContainer->SetMargin(textMargins);
	textContainer->callback = METHOD_CALLBACK(&TextArea::TextChanged, this);
	if (Flags()&Editable) {
		textContainer->SetFlags(View::IgnoreEvents, OP_NAND);
		SetEventProxy(textContainer);
	} else {
		textContainer->SetFlags(View::IgnoreEvents, OP_OR);
		textContainer->SetEventProxy(&scrollview);
		SetEventProxy(&scrollview);
	}
	scrollview.AddSubviewInFrontOfView(textContainer);

	UpdateScrollview();
	scrollview.ScrollTo(Point());
}

}
