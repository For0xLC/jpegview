#include "StdAfx.h"
#include "EXIFDisplay.h"
#include "Helpers.h"
#include "HelpersGUI.h"
#include "HistogramCorr.h"
#include "NLS.h"
#include <math.h>

constexpr auto BUTTON_SIZE = 18;

static LPTSTR CopyStrAlloc(LPCTSTR str)
{
	if (str == NULL) {
		return NULL;
	}
	int nLen = (int)_tcslen(str);
	TCHAR* pNewStr = new TCHAR[nLen + 1];
	_tcscpy_s(pNewStr, nLen + 1, str);
	return pNewStr;
}

static CRect InflateRect(const CRect& rect, float fAmount)
{
	CRect r(rect);
	int nAmount = (int)(fAmount * r.Width());
	r.InflateRect(-nAmount, -nAmount);
	return r;
}

CEXIFDisplay::CEXIFDisplay(HWND hWnd, INotifiyMouseCapture* pNotifyMouseCapture) : CPanel(hWnd, pNotifyMouseCapture, true, true)
{
	m_bFixedWidth = false;
	m_nMaxWidth = 0;
	m_nContentMaxWidth = 0;

	m_nGap = (int)(m_fDPIScale * 10);
	m_nTab1 = 0;
	m_pos = CPoint(0, 0);
	m_size = CSize(0, 0);

	m_sPrefix = NULL;
	m_nPrefixWidth = 0;

	m_sTitle = NULL;
	m_nTitleWidth = 0;
	m_nTitleHeight = 0;
	m_hTitleFont = 0;
	// m_bTitleSingleLine = true;

	m_sComment = NULL;
	m_nCommentWidth = 0;
	m_nCommentHeight = 0;
	m_nCommentMaxLines = 0;

	m_lines = {};
	m_nLineHeight = 0;

	m_bShowHistogram = false;
	m_nHistogramWidth = HelpersGUI::ScaleToScreen(256);
	m_nHistogramHeight = HelpersGUI::ScaleToScreen(50);
	m_pHistogram = NULL;

	AddUserPaintButton(ID_btnShowHideHistogram, &ShowHistogramTooltip, &PaintShowHistogramBtn, NULL, this, this);
	AddUserPaintButton(ID_btnClose, CNLS::GetString(_T("Close")), &PaintCloseBtn, NULL, this, this);
	CURLCtrl* pLinkLocation = AddURL(ID_urlLocation, _T(""), _T(""), false);
	pLinkLocation->SetShow(false, false);
}

CEXIFDisplay::~CEXIFDisplay()
{
	ClearTexts();
	if (m_hTitleFont != 0)
	{
		::DeleteObject(m_hTitleFont);
	}
}

void CEXIFDisplay::ClearTexts()
{
	delete[] m_sPrefix;
	m_sPrefix = NULL;
	delete[] m_sTitle;
	m_sTitle = NULL;
	delete[] m_sComment;
	m_sComment = NULL;

	m_nPrefixWidth = 0;
	m_nTitleWidth = 0;
	m_nTitleHeight = 0;

	m_nCommentWidth = 0;
	m_nCommentHeight = 0;

	m_nLineHeight = 0;

	std::list<TextLine>::iterator iter;
	for (iter = m_lines.begin( ); iter != m_lines.end( ); iter++ )
	{
		delete[] iter->Desc;
		delete[] iter->Value;
	}
	m_lines.clear();

	CUICtrl* pLinkLocation = GetControl(CEXIFDisplay::ID_urlLocation);
	pLinkLocation->SetShow(false, false);

	RequestRepositioning();
}

void CEXIFDisplay::AddTitle(LPCTSTR sTitle) {
	delete[] m_sTitle;
	m_sTitle = CopyStrAlloc(sTitle);
}

void CEXIFDisplay::AddPrefix(LPCTSTR sPrefix) {
	delete[] m_sPrefix;
	m_sPrefix = CopyStrAlloc(sPrefix);
}

void CEXIFDisplay::SetComment(LPCTSTR sComment) {
	delete[] m_sComment;
	CString s(sComment);
	s.TrimLeft();
	s.TrimRight();
	m_sComment = (s.GetLength() == 0) ? NULL : CopyStrAlloc(s);
}

void CEXIFDisplay::SetGPSLocation(LPCTSTR sLocation, LPCTSTR sURL) {
	CURLCtrl* pLinkLocation = GetControl<CURLCtrl*>(CEXIFDisplay::ID_urlLocation);
	pLinkLocation->SetText(sLocation);
	pLinkLocation->SetURL(sURL);
	pLinkLocation->SetShow(true, false);
}

void CEXIFDisplay::AddLine(LPCTSTR sDescription, LPCTSTR sValue, bool valueIsURL) {
	m_lines.push_back(TextLine(CopyStrAlloc(sDescription), CopyStrAlloc(sValue), valueIsURL));
}

void CEXIFDisplay::AddLine(LPCTSTR sDescription, double dValue, int nDigits) {
	TCHAR buffFormat[8];
	_stprintf_s(buffFormat, 8, _T("%%.%df"), nDigits);
	TCHAR buff[32];
	_stprintf_s(buff, 32, buffFormat, dValue);
	AddLine(sDescription, buff);
}

void CEXIFDisplay::AddLine(LPCTSTR sDescription, int nValue){
	TCHAR buff[32];
	_stprintf_s(buff, 32, _T("%d"), nValue);
	AddLine(sDescription, buff);
}

void CEXIFDisplay::AddLine(LPCTSTR sDescription, const SYSTEMTIME &time) {
	CString sTime = Helpers::SystemTimeToString(time);
	AddLine(sDescription, sTime);
}

void CEXIFDisplay::AddLine(LPCTSTR sDescription, const FILETIME &time) {
	SYSTEMTIME systemTime;
	::FileTimeToSystemTime(&time, &systemTime);
	TIME_ZONE_INFORMATION tzi;
	::GetTimeZoneInformation(&tzi);
	::SystemTimeToTzSpecificLocalTime(&tzi, &systemTime, &systemTime);
	AddLine(sDescription, systemTime);
}

void CEXIFDisplay::AddLine(LPCTSTR sDescription, const Rational &number) {
	if (number.Denominator == 1) {
		AddLine(sDescription, number.Numerator);
	} else if (number.Numerator > 9) {
		TCHAR buff[32];
		if (number.Numerator * 3 < number.Denominator) {
			_stprintf_s(buff, 32, _T("1/%d"), number.Denominator / number.Numerator);
		} else {
			_stprintf_s(buff, 32, _T("%.2f"), double(number.Numerator) / number.Denominator);
		}
		AddLine(sDescription, buff);
	} else {
		TCHAR buff[32];
		_stprintf_s(buff, 32, _T("%d/%d"), number.Numerator, number.Denominator);
		AddLine(sDescription, buff);
	}
}

void CEXIFDisplay::RequestRepositioning()
{
	m_nLineHeight = 0;
}

CRect CEXIFDisplay::PanelRect()
{
	if (m_nLineHeight != 0)
	{
		return CRect(m_pos, m_size);
	}

	return m_bFixedWidth ? PanelRectFixed() : PanelRectVariable();
}

CRect CEXIFDisplay::PanelRectFixed()
{
	if (m_nLineHeight == 0)
	{
		CDC dc(::GetDC(m_hWnd));
		HelpersGUI::SelectDefaultGUIFont(dc);

		if (m_hTitleFont == 0)
		{
			m_hTitleFont = HelpersGUI::CreateBoldFontOfSelectedFont(dc);
		}

		if (m_hTitleFont != 0)
		{
			::SelectObject(dc, m_hTitleFont);
		}

		m_nContentMaxWidth = m_nMaxWidth - 2 * m_nGap;

		m_nTitleHeight = 0;
		m_nPrefixWidth = 0;
		m_nTitleWidth = 0;
		CSize size = CSize(0, 0);

		if (m_sPrefix != NULL)
		{
			::GetTextExtentPoint32(dc, m_sPrefix, (int)_tcslen(m_sPrefix), &size);
			m_nPrefixWidth = size.cx;
			m_nTitleHeight = size.cy;
		}

		if (m_sTitle != NULL)
		{
			int nMaxTitleWidth = m_nContentMaxWidth - m_nPrefixWidth - (m_nGap >> 1);

			::GetTextExtentPoint32(dc, m_sTitle, (int)_tcslen(m_sTitle), &size);

			if (size.cx > nMaxTitleWidth)
			{
				// m_bTitleSingleLine = false;
				CRect rectTitle(0, 0, nMaxTitleWidth, INT_MAX);
				::DrawText(dc, m_sTitle, (int)_tcslen(m_sTitle), &rectTitle, DT_CALCRECT | DT_NOPREFIX | DT_WORDBREAK);
				m_nTitleWidth = rectTitle.Width();
				m_nTitleHeight = max(m_nTitleHeight, rectTitle.Height());
			}
			else
			{
				// m_bTitleSingleLine = true;
				m_nTitleWidth = size.cx;
				m_nTitleHeight = max(m_nTitleHeight, size.cy);
			}
		}

		HelpersGUI::SelectDefaultGUIFont(dc);

		int nMaxDescWidth = 0;
		int nMaxValueWidth = 0;
		std::list<TextLine>::iterator iter;

		for (iter = m_lines.begin(); iter != m_lines.end(); iter++)
		{
			if (iter->Desc != NULL)
			{
				::GetTextExtentPoint32(dc, iter->Desc, (int)_tcslen(iter->Desc), &size);
				nMaxDescWidth = max(nMaxDescWidth, size.cx);
				m_nLineHeight = max(m_nLineHeight, size.cy);
			}
			if (iter->Value != NULL)
			{
				::GetTextExtentPoint32(dc, iter->Value, (int)_tcslen(iter->Value), &size);
				nMaxValueWidth = max(nMaxValueWidth, size.cx);
				m_nLineHeight = max(m_nLineHeight, size.cy);
			}
		}

		m_nTab1 = nMaxDescWidth + m_nGap;
		m_nTab1 = (m_nTab1 > (m_nMaxWidth >> 1)) ? (m_nMaxWidth >> 1) : m_nTab1;

		m_nCommentHeight = 0;
		bool bHasComment = (m_sComment != NULL);

		if (bHasComment)
		{
			CRect rectComment(0, 0, m_nContentMaxWidth, INT_MAX);
			::DrawText(dc, m_sComment, (int)_tcslen(m_sComment), &rectComment, DT_CALCRECT | DT_NOPREFIX | DT_WORDBREAK);

			m_nCommentWidth = rectComment.Width();

			if (m_nCommentMaxLines == -1)
			{
				m_nCommentHeight = rectComment.Height();
			}
			else
			{
				int nMaxCommentHeight = m_nCommentMaxLines * m_nLineHeight;
				m_nCommentHeight = min(rectComment.Height(), nMaxCommentHeight);
			}
		}

		int nExpansionX = 0;
		int	nExpansionY = 0;

		if (m_bShowHistogram)
		{
			nExpansionX = max(0, m_nHistogramWidth - m_nContentMaxWidth);
			nExpansionY = m_nHistogramHeight + m_nGap;
		}

		int nPanelWidth = m_nMaxWidth;
		int nPanelHeight = m_nGap + m_nTitleHeight + (m_nGap >> 1);

		if (bHasComment) nPanelHeight += m_nCommentHeight + (m_nGap >> 1); // + Comment
		nPanelHeight += (int)m_lines.size() * m_nLineHeight + m_nGap;  // + EXIF

		if (!m_bShowHistogram)
		{
			m_size = CSize(nPanelWidth, nPanelHeight);
		}
		else
		{
			m_size = CSize(nPanelWidth + nExpansionX, nPanelHeight + nExpansionY);
		}

		::ReleaseDC(m_hWnd, dc);
	}
	return CRect(m_pos, m_size);
}

CRect CEXIFDisplay::PanelRectVariable()
{
	if (m_nLineHeight == 0)
	{
		CDC dc(::GetDC(m_hWnd));
		HelpersGUI::SelectDefaultGUIFont(dc);

		if (m_hTitleFont == 0)
		{
			m_hTitleFont = HelpersGUI::CreateBoldFontOfSelectedFont(dc);
		}

		if (m_hTitleFont != 0)
		{
			::SelectObject(dc, m_hTitleFont);
		}

		m_nContentMaxWidth = m_nMaxWidth - 2 * m_nGap;

		m_nTitleHeight = 0;
		m_nPrefixWidth = 0;
		m_nTitleWidth = 0;
		CSize size = CSize(0, 0);

		if (m_sPrefix != NULL)
		{
			::GetTextExtentPoint32(dc, m_sPrefix, (int)_tcslen(m_sPrefix), &size);
			m_nPrefixWidth = size.cx;
			m_nTitleHeight = size.cy;
		}

		if (m_sTitle != NULL)
		{
			int nMaxTitleWidth = m_nContentMaxWidth - m_nPrefixWidth - (m_nGap >> 1);

			::GetTextExtentPoint32(dc, m_sTitle, (int)_tcslen(m_sTitle), &size);

			if (size.cx > nMaxTitleWidth)
			{
				// m_bTitleSingleLine = false;
				CRect rectTitle(0, 0, nMaxTitleWidth, INT_MAX);
				::DrawText(dc, m_sTitle, (int)_tcslen(m_sTitle), &rectTitle, DT_CALCRECT | DT_NOPREFIX | DT_WORDBREAK);
				m_nTitleWidth = rectTitle.Width();
				m_nTitleHeight = max(m_nTitleHeight, rectTitle.Height());
			}
			else
			{
				// m_bTitleSingleLine = true;
				m_nTitleWidth = size.cx;
				m_nTitleHeight = max(m_nTitleHeight, size.cy);
			}
		}

		HelpersGUI::SelectDefaultGUIFont(dc);

		int nMaxDescWidth = 0;
		int nMaxValueWidth = 0;
		std::list<TextLine>::iterator iter;

		for (iter = m_lines.begin(); iter != m_lines.end(); iter++)
		{
			if (iter->Desc != NULL)
			{
				::GetTextExtentPoint32(dc, iter->Desc, (int)_tcslen(iter->Desc), &size);
				nMaxDescWidth = max(nMaxDescWidth, size.cx);
				m_nLineHeight = max(m_nLineHeight, size.cy);
			}
			if (iter->Value != NULL)
			{
				::GetTextExtentPoint32(dc, iter->Value, (int)_tcslen(iter->Value), &size);
				nMaxValueWidth = max(nMaxValueWidth, size.cx);
				m_nLineHeight = max(m_nLineHeight, size.cy);
			}
		}

		m_nTab1 = nMaxDescWidth + m_nGap;
		m_nTab1 = (m_nTab1 > (m_nMaxWidth >> 1)) ? (m_nMaxWidth >> 1) : m_nTab1;

		m_nCommentHeight = 0;
		bool bHasComment = (m_sComment != NULL);

		if (bHasComment)
		{
			CRect rectComment(0, 0, m_nContentMaxWidth, INT_MAX);
			::DrawText(dc, m_sComment, (int)_tcslen(m_sComment), &rectComment, DT_CALCRECT | DT_NOPREFIX | DT_WORDBREAK);

			m_nCommentWidth = rectComment.Width();

			if (m_nCommentMaxLines == -1)
			{
				m_nCommentHeight = rectComment.Height();
			}
			else
			{
				int nMaxCommentHeight = m_nCommentMaxLines * m_nLineHeight;
				m_nCommentHeight = min(rectComment.Height(), nMaxCommentHeight);
			}
		}

		int nExpansionX = 0;
		int	nExpansionY = 0;

		if (m_bShowHistogram)
		{
			nExpansionX = max(0, m_nHistogramWidth - m_nContentMaxWidth);
			nExpansionY = m_nHistogramHeight + m_nGap;
		}

		int nPanelWidth = max(m_nPrefixWidth + (m_nGap >> 1) + m_nTitleWidth, nMaxDescWidth + nMaxValueWidth);
		nPanelWidth = max(nPanelWidth, m_nCommentWidth);
		nPanelWidth = min(nPanelWidth, m_nContentMaxWidth);
		nPanelWidth += 2 * m_nGap;

		int nPanelHeight = m_nGap + m_nTitleHeight + (m_nGap >> 1);
		if (bHasComment) nPanelHeight += m_nCommentHeight + (m_nGap >> 1); // + Comment
		nPanelHeight += (int)m_lines.size() * m_nLineHeight + m_nGap;  // + EXIF

		if (!m_bShowHistogram)
		{
			m_size = CSize(nPanelWidth, nPanelHeight);
		}
		else
		{
			m_size = CSize(nPanelWidth + nExpansionX, nPanelHeight + nExpansionY);
		}

		::ReleaseDC(m_hWnd, dc);
	}
	return CRect(m_pos, m_size);
}

void CEXIFDisplay::OnPaint(CDC & dc, const CPoint& offset)
{
	CURLCtrl* pLinkLocation = GetControl<CURLCtrl*>(CEXIFDisplay::ID_urlLocation);
	bool isLinkLocationShown = pLinkLocation->IsShown();
	pLinkLocation->SetShow(false, false);

	CPanel::OnPaint(dc, offset);

	int nX = m_pos.x + offset.x;
	int nY = m_pos.y + offset.y;

	if (m_hTitleFont != 0)
	{
		::SelectObject(dc, m_hTitleFont);
	}

	::SetBkMode(dc, TRANSPARENT);
	::SetTextColor(dc, RGB(255, 255, 255));

	int nXEdge = nX + m_nGap + m_size.cx - 2 * m_nGap;

	if (m_sPrefix != NULL)
	{
		::TextOut(dc, nX + m_nGap, nY + m_nGap, m_sPrefix, (int)_tcslen(m_sPrefix));
	}

	int nRunningY = nY + m_nGap;

	if (m_sTitle != NULL)
	{
		int nXStart = nX + m_nGap + m_nPrefixWidth + (m_nGap >> 1);
		CRect rectTitle(nXStart, nRunningY, nXEdge, nRunningY + m_nTitleHeight);
		::DrawText(dc, m_sTitle, (int)_tcslen(m_sTitle), &rectTitle, DT_NOPREFIX | DT_WORDBREAK);
	}

	::SetTextColor(dc, RGB(243, 242, 231));
	HelpersGUI::SelectDefaultGUIFont(dc);

	nRunningY += m_nTitleHeight + (m_nGap >> 1);

	if (m_sComment != NULL)
	{
		CRect rectComment(nX + m_nGap, nRunningY, nXEdge, nRunningY + m_nCommentHeight);
		::DrawText(dc, m_sComment, (int)_tcslen(m_sComment), &rectComment, DT_NOPREFIX | DT_WORDBREAK | DT_END_ELLIPSIS);
		nRunningY += m_nCommentHeight + (m_nGap >> 1);
	}

	std::list<TextLine>::iterator iter;
	for (iter = m_lines.begin(); iter != m_lines.end(); iter++ )
	{
		if (iter->Desc != NULL)
		{
			CRect rectDesc(nX + m_nGap, nRunningY, nX  + m_nTab1, nRunningY + m_nLineHeight);
			::DrawText(dc, iter->Desc, (int)_tcslen(iter->Desc), &rectDesc, DT_NOPREFIX | DT_WORD_ELLIPSIS);
		}
		if (iter->Value != NULL)
		{
			if (iter->ValueIsURL)
			{
				pLinkLocation->SetShow(isLinkLocationShown, false);
				pLinkLocation->SetPosition(CRect(CPoint(nX + m_nTab1 - offset.x, nRunningY - offset.y), pLinkLocation->GetMinSize()));
				if (isLinkLocationShown) pLinkLocation->OnPaint(dc, offset);
				::SetTextColor(dc, RGB(243, 242, 231));
				HelpersGUI::SelectDefaultGUIFont(dc);
			}
			else
			{
				CRect rectValue(nX + m_nTab1, nRunningY, nXEdge, nRunningY + m_nLineHeight);
				::DrawText(dc, iter->Value, (int)_tcslen(iter->Value), &rectValue, DT_NOPREFIX | DT_WORD_ELLIPSIS);
			}
		}
		nRunningY += m_nLineHeight;
	}

	if (m_bShowHistogram)
	{
		::SelectObject(dc, ::GetStockObject(WHITE_PEN));

		int nHistogramWidth = m_nHistogramWidth;
		int nHistogramYBase = nY + m_size.cy - m_nGap;
		int nHistogramXBase = nX + (m_size.cx - nHistogramWidth) / 2;

		dc.MoveTo(nHistogramXBase, nHistogramYBase);
		dc.LineTo(nHistogramXBase + m_nHistogramWidth, nHistogramYBase);

		if (m_pHistogram != NULL)
		{
			PaintHistogram(dc, nHistogramXBase, nHistogramYBase);
		}
	}
}

void CEXIFDisplay::PaintHistogram(CDC & dc, int nXStart, int nYBaseLine)
{
	const int* pChannelGrey = m_pHistogram->GetChannelGrey();
	int nMaxValue = 0;
	for (int i = 0; i < 256; i++) {
		nMaxValue = max(pChannelGrey[i], nMaxValue);
	}
	double dScaling = (nMaxValue == 0) ? 0.0f : m_nHistogramHeight / sqrt((double)nMaxValue);

	HPEN hPen = ::CreatePen(PS_SOLID, 1, RGB(190, 190, 170));
	HGDIOBJ hOldPen = ::SelectObject(dc, hPen);
	int length = m_nHistogramWidth;
	float reductionFactor = 256.0f / length;
	for (int i = 0; i < length; i++) {
		int nLineHeight = (int)(sqrt((double)pChannelGrey[(int)(i * reductionFactor)]) * dScaling + 0.5);
		dc.MoveTo(nXStart + i, nYBaseLine - nLineHeight);
		dc.LineTo(nXStart + i, nYBaseLine);
	}
	::SelectObject(dc, hOldPen);
	::DeleteObject(hPen);
}

void CEXIFDisplay::RepositionAll()
{
	CRect panelRect = PanelRect();

	CUICtrl* pButton = GetControl(ID_btnShowHideHistogram);

	if (pButton != NULL)
	{
		int nButtonSize = (int)(m_fDPIScale * BUTTON_SIZE);
		int nX = panelRect.right - m_nGap - nButtonSize;
		int nY = panelRect.bottom - m_nGap - nButtonSize;
		nY -= m_bShowHistogram ? m_nHistogramHeight + m_nGap : 0;
		pButton->SetPosition(CRect(CPoint(nX, nY), CSize(nButtonSize, nButtonSize)));
	}

	CUICtrl* pButtonClose = GetControl(ID_btnClose);

	if (pButtonClose != NULL)
	{
		int nButtonSize = (int)(m_fDPIScale * BUTTON_SIZE * 0.9f);
		pButtonClose->SetPosition(CRect(CPoint(panelRect.right - nButtonSize, panelRect.top), CSize(nButtonSize, nButtonSize)));
	}
}

static void PaintShowHistogramBtnOnePass(CDC& dc, const CRect& r, bool bArrowDown)
{
	CPoint p1(r.left + 1, r.top + 1);
	CPoint p2(r.right, r.top);
	int nMiddleX = (r.left + r.right) / 2;
	CPoint p3(nMiddleX, r.top + (r.right - r.left) / 2);
	if (bArrowDown) {
		dc.MoveTo(CPoint(p1.x, p3.y));
		dc.LineTo(CPoint(p3.x, p1.y));
		dc.MoveTo(CPoint(p3.x, p1.y));
		dc.LineTo(CPoint(p2.x, p3.y + 1));
	} else {
		dc.MoveTo(p1);
		dc.LineTo(p3);
		dc.MoveTo(p3);
		dc.LineTo(p2);
	}
}

void CEXIFDisplay::PaintShowHistogramBtn(void* pContext, const CRect& rect, CDC& dc)
{
	CEXIFDisplay* pThis = (CEXIFDisplay*)pContext;
	CRect r = InflateRect(rect, 0.3f);
	r.OffsetRect(CPoint(0, 1));
	PaintShowHistogramBtnOnePass(dc, r, pThis->GetShowHistogram());
	if (HelpersGUI::ScreenScaling >= 2)
	{
		r.OffsetRect(0, 1);
		PaintShowHistogramBtnOnePass(dc, r, pThis->GetShowHistogram());
	}
}

static void PaintCloseBtnOnePass(CDC& dc, const CRect& r)
{
	CPoint p1(r.left + 1, r.top + 1);
	dc.MoveTo(p1);
	CPoint p2(r.right, r.bottom);
	dc.LineTo(p2);
	CPoint p3(r.right - 1, r.top + 1);
	dc.MoveTo(p3);
	CPoint p4(r.left, r.bottom);
	dc.LineTo(p4);
}

void CEXIFDisplay::PaintCloseBtn(void* pContext, const CRect& rect, CDC& dc)
{
	CRect r = Helpers::InflateRect(rect, 0.25f);
	PaintCloseBtnOnePass(dc, r);
	if (HelpersGUI::ScreenScaling >= 2) {
		r.OffsetRect(0, 1);
		PaintCloseBtnOnePass(dc, r);
	}
}

LPCTSTR CEXIFDisplay::ShowHistogramTooltip(void* pContext)
{
	CEXIFDisplay* pThis = (CEXIFDisplay*)pContext;
	if (pThis->GetShowHistogram()) {
		return CNLS::GetString(_T("Hide histogram"));
	} else {
		return CNLS::GetString(_T("Show histogram"));
	}
}
