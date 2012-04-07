/*
** Taiga, a lightweight client for MyAnimeList
** Copyright (C) 2010-2012, Eren Okka
** 
** This program is free software: you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation, either version 3 of the License, or
** (at your option) any later version.
** 
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
** 
** You should have received a copy of the GNU General Public License
** along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "../std.h"
#include "../animelist.h"
#include "../common.h"
#include "dlg_search.h"
#include "../http.h"
#include "../myanimelist.h"
#include "../resource.h"
#include "../settings.h"
#include "../string.h"
#include "../taiga.h"
#include "../theme.h"
#include "../win32/win_taskdialog.h"
#include "../xml.h"

class SearchDialog SearchDialog;

// =============================================================================

SearchDialog::SearchDialog() {
  RegisterDlgClass(L"TaigaSearchW");
}

BOOL SearchDialog::OnInitDialog() {
  // Create list control
  list_.Attach(GetDlgItem(IDC_LIST_SEARCH));
  list_.SetExtendedStyle(LVS_EX_DOUBLEBUFFER | LVS_EX_FULLROWSELECT | LVS_EX_INFOTIP | LVS_EX_LABELTIP);
  list_.SetImageList(UI.ImgList16.GetHandle());
  list_.SetTheme();
  list_.InsertColumn(0, 280, 280, LVCFMT_LEFT,   L"Anime title");
  list_.InsertColumn(1,  60,  60, LVCFMT_CENTER, L"Type");
  list_.InsertColumn(2,  60,  60, LVCFMT_RIGHT,  L"Episodes");
  list_.InsertColumn(3,  60,  60, LVCFMT_RIGHT,  L"Score");
  list_.InsertColumn(4, 100, 100, LVCFMT_RIGHT,  L"Season");

  // Create edit control
  edit_.Attach(GetDlgItem(IDC_EDIT_SEARCH));
  
  // Success
  return TRUE;
}

void SearchDialog::OnOK() {
  wstring text;
  edit_.GetText(text);
  Search(text);
}

INT_PTR SearchDialog::DialogProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
  switch (uMsg) {
    // Forward mouse wheel messages to the list
    case WM_MOUSEWHEEL: {
      return list_.SendMessage(uMsg, wParam, lParam);
    }
  }
  
  return DialogProcDefault(hwnd, uMsg, wParam, lParam);
}

LRESULT SearchDialog::OnNotify(int idCtrl, LPNMHDR pnmh) {
  // ListView control
  if (idCtrl == IDC_LIST_SEARCH) {
    switch (pnmh->code) {
      // Column click
      case LVN_COLUMNCLICK: {
        LPNMLISTVIEW lplv = (LPNMLISTVIEW)pnmh;
        int order = 1;
        if (lplv->iSubItem == list_.GetSortColumn()) order = list_.GetSortOrder() * -1;
        switch (lplv->iSubItem) {
          // Episode
          case 2:
            list_.Sort(lplv->iSubItem, order, LIST_SORTTYPE_NUMBER, ListViewCompareProc);
            break;
          // Season
          case 4:
            list_.Sort(lplv->iSubItem, order, LIST_SORTTYPE_STARTDATE, ListViewCompareProc);
            break;
          // Other columns
          default:
            list_.Sort(lplv->iSubItem, order, LIST_SORTTYPE_DEFAULT, ListViewCompareProc);
            break;
        }
        break;
      }

      // Double click
      case NM_DBLCLK: {
        if (list_.GetSelectedCount() > 0) {
          LPNMITEMACTIVATE lpnmitem = reinterpret_cast<LPNMITEMACTIVATE>(pnmh);
          if (lpnmitem->iItem == -1) break;
          LPARAM lParam = list_.GetItemParam(lpnmitem->iItem);
          if (lParam) ExecuteAction(L"Info", 0, lParam);
        }
        break;
      }

      // Right click
      case NM_RCLICK: {
        LPNMITEMACTIVATE lpnmitem = reinterpret_cast<LPNMITEMACTIVATE>(pnmh);
        if (lpnmitem->iItem == -1) break;
        LPARAM lParam = list_.GetItemParam(lpnmitem->iItem);
        if (!lParam) break;
        UpdateSearchListMenu(reinterpret_cast<Anime*>(lParam)->index <= 0);
        ExecuteAction(UI.Menus.Show(pnmh->hwndFrom, 0, 0, L"SearchList"), 0, lParam);
        break;
      }

      // Custom draw
      case NM_CUSTOMDRAW: {
        LPNMLVCUSTOMDRAW pCD = reinterpret_cast<LPNMLVCUSTOMDRAW>(pnmh);
        switch (pCD->nmcd.dwDrawStage) {
          case CDDS_PREPAINT:
            return CDRF_NOTIFYITEMDRAW;
          case CDDS_ITEMPREPAINT:
            return CDRF_NOTIFYSUBITEMDRAW;
          case CDDS_PREERASE:
          case CDDS_ITEMPREERASE:
            return CDRF_NOTIFYPOSTERASE;
          case CDDS_ITEMPREPAINT | CDDS_SUBITEM: {
            // Alternate background color
            if ((pCD->nmcd.dwItemSpec % 2) && !list_.IsGroupViewEnabled()) {
              pCD->clrTextBk = RGB(248, 248, 248);
            }
            // Change text color
            Anime* anime = reinterpret_cast<Anime*>(pCD->nmcd.lItemlParam);
            if (anime) {
              if (anime->index > 0) {
                pCD->clrText = RGB(180, 180, 180);
              } else {
                pCD->clrText = GetSysColor(COLOR_WINDOWTEXT);
              }
            }
            return CDRF_NOTIFYPOSTPAINT;
          }
        }
      }
    }
  }

  return 0;
}

BOOL SearchDialog::PreTranslateMessage(MSG* pMsg) {
  if (pMsg->message == WM_KEYDOWN) {
    switch (pMsg->wParam) {
      // Close window
      case VK_ESCAPE: {
        Destroy();
        return TRUE;
      }
      // Search
      case VK_RETURN: {
        if (::GetFocus() == edit_.GetWindowHandle()) {
          OnOK();
          return TRUE;
        }
        break;
      }
    }
  }
  return FALSE;
}

// =============================================================================

void SearchDialog::EnableInput(bool enable) {
  EnableDlgItem(IDOK, enable);
  SetDlgItemText(IDOK, enable ? L"Search" : L"Searching...");
}

void SearchDialog::ParseResults(const wstring& data) {
  if (data.empty()) {
    wstring msg;
    edit_.GetText(msg);
    msg = L"No results found for \"" + msg + L"\".";
    win32::TaskDialog dlg(L"Search Anime", TD_INFORMATION_ICON);
    dlg.SetMainInstruction(msg.c_str());
    dlg.AddButton(L"OK", IDOK);
    dlg.Show(GetWindowHandle());
    return;
  }
  if (data == L"Invalid credentials") {
    win32::TaskDialog dlg(L"Search Anime", TD_ERROR_ICON);
    dlg.SetMainInstruction(L"Invalid user name or password.");
    dlg.SetContent(L"Anime search requires authentication, which means, "
      L"you need to enter a valid user name and password to search MyAnimeList.");
    dlg.AddButton(L"OK", IDOK);
    dlg.Show(GetWindowHandle());
    return;
  }
  anime_results_.clear();
  
  xml_document doc;
  xml_parse_result result = doc.load(data.c_str());
  if (result.status == status_ok) {
    xml_node anime = doc.child(L"anime");
    for (xml_node entry = anime.child(L"entry"); entry; entry = entry.next_sibling(L"entry")) {
      int i = anime_results_.size(); anime_results_.resize(i + 1);
      int anime_id = XML_ReadIntValue(entry, L"id");
      anime_results_[i].index = AnimeList.FindItemIndex(anime_id);
      
      if (anime_results_[i].index > -1) {
        AnimeList.items[anime_results_[i].index].score = XML_ReadStrValue(entry, L"score");
        AnimeList.items[anime_results_[i].index].synopsis = XML_ReadStrValue(entry, L"synopsis");
        mal::DecodeText(AnimeList.items[anime_results_[i].index].synopsis);
      } else {
        anime_results_[i].series_id = anime_id;
        anime_results_[i].series_title = XML_ReadStrValue(entry, L"title");
        mal::DecodeText(anime_results_[i].series_title);
        anime_results_[i].series_synonyms = XML_ReadStrValue(entry, L"synonyms");
        mal::DecodeText(anime_results_[i].series_synonyms);
        anime_results_[i].series_episodes = XML_ReadIntValue(entry, L"episodes");
        anime_results_[i].score = XML_ReadStrValue(entry, L"score");
        anime_results_[i].series_type = mal::TranslateType(XML_ReadStrValue(entry, L"type"));
        anime_results_[i].series_status = mal::TranslateStatus(XML_ReadStrValue(entry, L"status"));
        anime_results_[i].series_start = XML_ReadStrValue(entry, L"start_date");
        anime_results_[i].series_end = XML_ReadStrValue(entry, L"end_date");
        anime_results_[i].synopsis = XML_ReadStrValue(entry, L"synopsis");
        mal::DecodeText(anime_results_[i].synopsis);
        anime_results_[i].series_image = XML_ReadStrValue(entry, L"image");
      }
    }
  }

  RefreshList();
}

void SearchDialog::RefreshList() {
  if (!IsWindow()) return;

  // Hide and clear the list
  list_.Hide();
  list_.DeleteAllItems();
  
  // Add anime items to list
  for (size_t i = 0; i < anime_results_.size(); i++) {
    if (anime_results_[i].index == -1) {
      anime_results_[i].index = AnimeList.FindItemIndex(anime_results_[i].series_id);
    }
    Anime* item = anime_results_[i].index > -1 ? &AnimeList.items[anime_results_[i].index] : &anime_results_[i];
    list_.InsertItem(i, -1, StatusToIcon(item->GetAiringStatus()), 0, nullptr, 
      item->series_title.c_str(), 
      reinterpret_cast<LPARAM>(item));
    list_.SetItem(i, 1, mal::TranslateType(item->series_type).c_str());
    list_.SetItem(i, 2, mal::TranslateNumber(item->series_episodes).c_str());
    list_.SetItem(i, 3, item->score.c_str());
    list_.SetItem(i, 4, mal::TranslateDateToSeason(item->series_start).c_str());
  }

  // Sort and show the list again
  list_.Sort(0, 1, LIST_SORTTYPE_DEFAULT, ListViewCompareProc);
  list_.Show();
}

bool SearchDialog::Search(const wstring& title) {
  if (mal::SearchAnime(title)) {
    edit_.SetText(title.c_str());
    EnableInput(false);
    list_.DeleteAllItems();
    anime_results_.clear();
    return true;
  } else {
    return false;
  }
}