#pragma once

#include "EXIFReader.h"
#include "Panel.h"

class CHistogram;

// Displays EXIF information on the screen (used when F2 is pressed)
class CEXIFDisplay : public CPanel
{
public:
	// IDs of the controls on this panel
	enum {
		ID_btnShowHideHistogram,
		ID_btnClose,
		ID_urlLocation
	};

public:
	CEXIFDisplay(HWND hWnd, INotifiyMouseCapture* pNotifyMouseCapture);
	~CEXIFDisplay();

	// Methods to create the image and EXIF data information lines
	void ClearTexts();
	void AddPrefix(LPCTSTR sPrefix);
	void AddTitle(LPCTSTR sTitle);
	void SetComment(LPCTSTR sComment);
	void SetGPSLocation(LPCTSTR sLocation, LPCTSTR sURL);
	void AddLine(LPCTSTR sDescription, LPCTSTR sValue, bool valueIsURL = false);
	void AddLine(LPCTSTR sDescription, double dValue, int nDigits);
	void AddLine(LPCTSTR sDescription, int nValue);
	void AddLine(LPCTSTR sDescription, const SYSTEMTIME &time); // time is in local time
	void AddLine(LPCTSTR sDescription, const FILETIME &time); // file time is in UTC
	void AddLine(LPCTSTR sDescription, const Rational &number);

	virtual void RequestRepositioning();
	void SetPosition(CPoint pos) { m_pos = pos; RepositionAll(); }

	virtual CRect PanelRect();
	CRect PanelRectFixed();
	CRect PanelRectVariable();

	virtual void OnPaint(CDC & dc, const CPoint& offset);

	void SetShowHistogram(bool bShow) { m_bShowHistogram = bShow; RepositionAll(); }
	bool GetShowHistogram() { return m_bShowHistogram; }

	// Note: The histogram is now owned by the panel
	void SetHistogram(const CHistogram* pHistogram) { m_pHistogram = pHistogram; }

protected:
	virtual void RepositionAll();

private:
	struct TextLine {
		TextLine(LPCTSTR desc, LPCTSTR value, bool valueIsURL = false) {
			Desc = desc;
			Value = value;
			ValueIsURL = valueIsURL;
		}

		LPCTSTR Desc;
		LPCTSTR Value;
		bool ValueIsURL;
	};

	bool m_bFixedWidth;
	int m_nMaxWidth;
	int m_nContentMaxWidth;

	int m_nGap;
	int m_nTab1;
	CPoint m_pos;
	CSize m_size;

	TCHAR* m_sPrefix;
	int m_nPrefixWidth;

	TCHAR* m_sTitle;
	int m_nTitleWidth;
	int m_nTitleHeight;
	HFONT m_hTitleFont;
	// bool m_bTitleSingleLine;

	TCHAR* m_sComment;
	int m_nCommentWidth;
	int m_nCommentHeight;
	int m_nCommentMaxLines;

	std::list<TextLine> m_lines;
	int m_nLineHeight;

	bool m_bShowHistogram;
	int m_nHistogramWidth;
	int m_nHistogramHeight;
	const CHistogram* m_pHistogram;

	void PaintHistogram(CDC & dc, int nXStart, int nYBaseLine);

	static void PaintShowHistogramBtn(void* pContext, const CRect& rect, CDC& dc);
	static void PaintCloseBtn(void* pContext, const CRect& rect, CDC& dc);
	static LPCTSTR ShowHistogramTooltip(void* pContext);
};
