﻿// This file is part of BowPad.
//
// Copyright (C) 2013-2020 - Stefan Kueng
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// See <http://www.gnu.org/licenses/> for a copy of the full license text
//

#include "stdafx.h"
#include "CmdRegexCapture.h"
#include "BowPad.h"
#include "ScintillaWnd.h"
#include "UnicodeUtils.h"
#include "StringUtils.h"
#include "OnOutOfScope.h"
#include "ResString.h"
#include "Theme.h"

#include <regex>
#include <algorithm>
#include <memory>

constexpr auto DEFAULT_MAX_SEARCH_STRINGS = 20;
constexpr auto TIMER_INFOSTRING           = 100;

std::unique_ptr<CRegexCaptureDlg> g_pRegexCaptureDlg;

void RegexCapture_Finish()
{
    g_pRegexCaptureDlg.reset();
}

CRegexCaptureDlg::CRegexCaptureDlg(void* obj)
    : ICommand(obj)
    , m_captureWnd(g_hRes)
    , m_themeCallbackId(-1)
    , m_maxRegexStrings(DEFAULT_MAX_SEARCH_STRINGS)
    , m_maxCaptureStrings(DEFAULT_MAX_SEARCH_STRINGS)
{
}

void CRegexCaptureDlg::Show()
{
    this->ShowModeless(g_hRes, IDD_REGEXCAPTUREDLG, GetHwnd());
    SetFocus(GetDlgItem(*this, IDC_REGEXCOMBO));
    UpdateWindow(*this);
}

LRESULT CRegexCaptureDlg::DlgFunc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
        case WM_SHOWWINDOW:
            //m_open = wParam != FALSE;
            break;
        case WM_DESTROY:
            break;
        case WM_INITDIALOG:
            DoInitDialog(hwndDlg);
            break;
        case WM_SIZE:
        {
            int newWidth  = LOWORD(lParam);
            int newHeight = HIWORD(lParam);
            m_resizer.DoResize(newWidth, newHeight);
            break;
        }
        case WM_COMMAND:
            return DoCommand(LOWORD(wParam), HIWORD(wParam));
        case WM_NOTIFY:
        {
            LPNMHDR pnmhdr = reinterpret_cast<LPNMHDR>(lParam);
            APPVERIFY(pnmhdr != nullptr);
            if (pnmhdr == nullptr)
                return 0;
            const NMHDR& nmhdr = *pnmhdr;

            if (nmhdr.idFrom == (UINT_PTR)&m_captureWnd || nmhdr.hwndFrom == m_captureWnd)
            {
                if (nmhdr.code == NM_COOLSB_CUSTOMDRAW)
                    return m_captureWnd.HandleScrollbarCustomDraw(wParam, (NMCSBCUSTOMDRAW*)lParam);
            }
        }
        break;
        case WM_TIMER:
            if (wParam == TIMER_INFOSTRING)
            {
                KillTimer(*this, TIMER_INFOSTRING);
                SetDlgItemText(*this, IDC_INFOLABEL, L"");
            }
            break;
    }
    return FALSE;
}

LRESULT CRegexCaptureDlg::DoCommand(int id, int /*msg*/)
{
    switch (id)
    {
        case IDCANCEL:
        {
            m_captureWnd.Call(SCI_CLEARALL);
            size_t lengthDoc = ScintillaCall(SCI_GETLENGTH);
            for (int i = INDIC_REGEXCAPTURE; i < INDIC_REGEXCAPTURE_END; ++i)
            {
                ScintillaCall(SCI_SETINDICATORCURRENT, i);
                ScintillaCall(SCI_INDICATORCLEARRANGE, 0, lengthDoc);
            }

            ShowWindow(*this, SW_HIDE);
        }
        break;
        case IDOK:
            DoCapture();
            break;
    }
    return 1;
}

void CRegexCaptureDlg::DoInitDialog(HWND hwndDlg)
{
    m_themeCallbackId = CTheme::Instance().RegisterThemeChangeCallback(
        [this]() {
            SetTheme(CTheme::Instance().IsDarkTheme());
        });
    m_captureWnd.Init(g_hRes, *this, GetDlgItem(*this, IDC_SCINTILLA));
    m_captureWnd.SetupLexerForLang("Text");
    SetTheme(CTheme::Instance().IsDarkTheme());
    InitDialog(hwndDlg, IDI_BOWPAD, false);
    m_captureWnd.SetupLexerForLang("Text");
    m_captureWnd.UpdateLineNumberWidth();

    // Position the dialog in the top right corner.
    // Make sure we don't obscure the scroll bar though.
    RECT rcScintilla, rcDlg;
    GetWindowRect(GetScintillaWnd(), &rcScintilla);
    GetWindowRect(hwndDlg, &rcDlg);

    int sbVertWidth = GetSystemMetrics(SM_CXVSCROLL);

    LONG adjustX = 15;
    if (sbVertWidth >= 0)
        adjustX += sbVertWidth;
    LONG x = rcScintilla.right - ((rcDlg.right - rcDlg.left) + adjustX);
    // Try (unscientifically) to not get to close to the tab bar either.
    LONG y = rcScintilla.top + 15;

    SetWindowPos(hwndDlg, HWND_TOP, x, y, 0, 0, SWP_NOSIZE | SWP_NOACTIVATE);

    AdjustControlSize(IDC_ICASE);
    AdjustControlSize(IDC_DOTNEWLINE);

    m_resizer.Init(hwndDlg);
    m_resizer.UseSizeGrip(!CTheme::Instance().IsDarkTheme());
    m_resizer.AddControl(hwndDlg, IDC_REGEXLABEL, RESIZER_TOPLEFT);
    m_resizer.AddControl(hwndDlg, IDC_CAPTURELABEL, RESIZER_TOPLEFT);
    m_resizer.AddControl(hwndDlg, IDC_REGEXCOMBO, RESIZER_TOPLEFTRIGHT);
    m_resizer.AddControl(hwndDlg, IDC_ICASE, RESIZER_TOPLEFTRIGHT);
    m_resizer.AddControl(hwndDlg, IDC_DOTNEWLINE, RESIZER_TOPLEFTRIGHT);
    m_resizer.AddControl(hwndDlg, IDC_CAPTURECOMBO, RESIZER_TOPLEFTRIGHT);
    m_resizer.AddControl(hwndDlg, IDOK, RESIZER_TOPRIGHT);
    m_resizer.AddControl(hwndDlg, IDC_SCINTILLA, RESIZER_TOPLEFTBOTTOMRIGHT);
    m_resizer.AdjustMinMaxSize();

    GetWindowRect(hwndDlg, &rcDlg);

    std::vector<std::wstring> regexStrings;
    m_maxRegexStrings = LoadData(regexStrings, DEFAULT_MAX_SEARCH_STRINGS, L"regexcapture", L"maxsearch", L"regex%d");
    LoadCombo(IDC_REGEXCOMBO, regexStrings);

    std::vector<std::wstring> captureStrings;
    m_maxCaptureStrings = LoadData(captureStrings, DEFAULT_MAX_SEARCH_STRINGS, L"regexcapture", L"maxsearch", L"capture%d");
    LoadCombo(IDC_CAPTURECOMBO, captureStrings);

    EnableComboBoxDeleteEvents(IDC_REGEXCOMBO, true);
    EnableComboBoxDeleteEvents(IDC_CAPTURECOMBO, true);
}

void CRegexCaptureDlg::SetTheme(bool bDark)
{
    CTheme::Instance().SetThemeForDialog(*this, bDark);
}

void CRegexCaptureDlg::DoCapture()
{
    SetDlgItemText(*this, IDC_INFOLABEL, L"");

    std::wstring sRegexW = GetDlgItemText(IDC_REGEXCOMBO).get();
    UpdateCombo(IDC_REGEXCOMBO, sRegexW, m_maxRegexStrings);
    std::wstring sCaptureW = GetDlgItemText(IDC_CAPTURECOMBO).get();
    UpdateCombo(IDC_CAPTURECOMBO, sCaptureW, m_maxCaptureStrings);

    auto sRegex   = CUnicodeUtils::StdGetUTF8(sRegexW);
    auto sCapture = UnEscape(CUnicodeUtils::StdGetUTF8(sCaptureW));
    try
    {
        auto                  findText = GetDlgItemText(IDC_SEARCHCOMBO);
        std::regex::flag_type rxFlags  = std::regex_constants::ECMAScript;
        if (IsDlgButtonChecked(*this, IDC_ICASE))
            rxFlags |= std::regex_constants::icase;
        // replace all "\n" chars with "(?:\n|\r\n|\n\r)"
        if ((sRegex.size() > 1) && (sRegex.find("\\r") == std::wstring::npos))
        {
            SearchReplace(sRegex, "\\n", "(!:\\n|\\r\\n|\\n\\r)");
        }

        const std::regex rx(sRegex, rxFlags);

        m_captureWnd.Call(SCI_CLEARALL);

        size_t lengthDoc = ScintillaCall(SCI_GETLENGTH);
        for (int i = INDIC_REGEXCAPTURE; i < INDIC_REGEXCAPTURE_END; ++i)
        {
            ScintillaCall(SCI_SETINDICATORCURRENT, i);
            ScintillaCall(SCI_INDICATORCLEARRANGE, 0, lengthDoc);
        }

        const char*                                          pText = (const char*)ScintillaCall(SCI_GETCHARACTERPOINTER);
        std::string_view                                     searchText(pText, lengthDoc);
        std::match_results<std::string_view::const_iterator> whatc;
        std::regex_constants::match_flag_type                flags = std::regex_constants::match_flag_type::match_default | std::regex_constants::match_flag_type::match_not_null;
        if (IsDlgButtonChecked(*this, IDC_DOTNEWLINE))
            flags |= std::regex_constants::match_flag_type::match_not_eol;
        auto                                         start = searchText.cbegin();
        auto                                         end   = searchText.cend();
        std::vector<std::tuple<sptr_t, size_t, size_t>> capturePositions;
        while (std::regex_search(start, end, whatc, rx, flags))
        {
            if (whatc[0].matched)
            {
                auto out = whatc.format(sCapture, flags);
                m_captureWnd.Call(SCI_APPENDTEXT, out.size(), (sptr_t)out.c_str());

                sptr_t captureCount = 0;
                for (const auto& w : whatc)
                {
                    capturePositions.push_back(std::make_tuple(captureCount, w.first - searchText.cbegin(), w.length()));
                    ++captureCount;
                }
            }
            // update search position:
            if (start == whatc[0].second)
            {
                if (start == end)
                    break;
                ++start;
            }
            else
                start = whatc[0].second;
            // update flags for continuation
            flags |= std::regex_constants::match_flag_type::match_prev_avail;
        }
        m_captureWnd.UpdateLineNumberWidth();

        for (const auto& [num, begin, length] : capturePositions)
        {
            ScintillaCall(SCI_SETINDICATORCURRENT, INDIC_REGEXCAPTURE + num);
            ScintillaCall(SCI_INDICATORFILLRANGE, begin, length);
        }

        std::vector<std::wstring> regexStrings;
        SaveCombo(IDC_REGEXCOMBO, regexStrings);
        SaveData(regexStrings, L"regexcapture", L"maxsearch", L"regex%d");
        std::vector<std::wstring> captureStrings;
        SaveCombo(IDC_CAPTURECOMBO, captureStrings);
        SaveData(captureStrings, L"regexcapture", L"maxsearch", L"capture%d");
    }
    catch (const std::exception&)
    {
        SetInfoText(IDS_REGEX_NOTOK, AlertMode::Flash);
    }
}

void CRegexCaptureDlg::SetInfoText(UINT resid, AlertMode alertMode)
{
    ResString str(g_hRes, resid);
    SetDlgItemText(*this, IDC_INFOLABEL, str);
    if (alertMode == AlertMode::Flash)
        FlashWindow(*this);
    SetTimer(*this, TIMER_INFOSTRING, 5000, nullptr);
}

CCmdRegexCapture::CCmdRegexCapture(void* obj)
    : ICommand(obj)
{
}

bool CCmdRegexCapture::Execute()
{
    if (!g_pRegexCaptureDlg)
        g_pRegexCaptureDlg = std::make_unique<CRegexCaptureDlg>(m_pMainWindow);

    g_pRegexCaptureDlg->Show();
    return true;
}
