/** @file
  Variable Application to List, Search, Create and Delete UEFI Variables.
  這是一個 UEFI 應用程式，用於列出、搜尋、建立和刪除 UEFI 變數。
**/

// 引入 UEFI 核心標頭檔，定義了基本資料型態 (如 EFI_STATUS, UINTN 等)
#include <Uefi.h>
// 引入 UEFI 函式庫，提供基本的 UEFI 功能封裝
#include <Library/UefiLib.h>
// 引入 Boot Services Table 函式庫，用於存取 gBS (Boot Services)
#include <Library/UefiBootServicesTableLib.h>
// 引入 Runtime Services Table 函式庫，用於存取 gRT (Runtime Services，如變數存取)
#include <Library/UefiRuntimeServicesTableLib.h>
// 引入基礎記憶體函式庫，用於記憶體操作 (如 ZeroMem, CopyMem)
#include <Library/BaseMemoryLib.h>
// 引入記憶體分配函式庫，用於動態配置記憶體 (如 AllocatePool, FreePool)
#include <Library/MemoryAllocationLib.h>
// 引入基礎函式庫，提供字串處理與轉換功能 (如 StrCmp, StrToGuid)
#include <Library/BaseLib.h>
// 引入列印函式庫，提供 Print 函式輸出文字到螢幕
#include <Library/PrintLib.h>

//
// 全域變數定義
//
// gDefaultVendorGuid: 定義一個預設的廠商 GUID (Vendor GUID)。
// 在 UEFI 中，變數由 "名稱 (Name)" 和 "廠商 GUID (Vendor GUID)" 共同唯一識別。
// 這裡定義了一個範例 GUID {37B93825-3B85-02D0-37B9-33F900000000} 用於測試。
EFI_GUID gDefaultVendorGuid = { 0x37B93825, 0x3B85, 0x02D0, { 0x37, 0xB9, 0x33, 0xF9, 0x00, 0x00, 0x00, 0x00 } };

//
// 結構定義
//
// VARIABLE_INFO: 自定義結構，用於在記憶體中暫存變數的資訊。
// 這用於 "ListAllVariables" 功能，先將所有變數讀入陣列，方便進行分頁顯示。
typedef struct {
  CHAR16   *Name;       // 指向變數名稱字串的指標 (動態分配)
  EFI_GUID Guid;        // 該變數所屬的 Vendor GUID
  UINTN    DataSize;    // 該變數內容的資料大小 (Bytes)
} VARIABLE_INFO;

//
// 輔助函式 (Helper Functions)
//

/**
  等待使用者按鍵輸入。
  此函式會暫停程式執行，直到鍵盤緩衝區中有按鍵事件發生。

  @return EFI_INPUT_KEY   回傳讀取到的按鍵資訊 (包含 ScanCode 和 UnicodeChar)。
**/
EFI_INPUT_KEY WaitKey() {
  UINTN Index;
  EFI_INPUT_KEY Key;

  // gBS->WaitForEvent: Boot Service 服務，用於等待事件。
  // 參數 1: 1 (要等待的事件數量)
  // 參數 2: &gST->ConIn->WaitForKey (要等待的事件陣列，這裡是 Console Input 的按鍵等待事件)
  // 參數 3: &Index (回傳觸發事件的索引值)
  gBS->WaitForEvent(1, &gST->ConIn->WaitForKey, &Index);

  // gST->ConIn->ReadKeyStroke: Console Input Protocol 的函式，從緩衝區讀取按鍵。
  // 參數 1: gST->ConIn (Console Input 實例)
  // 參數 2: &Key (輸出參數，儲存讀取到的按鍵資料)
  gST->ConIn->ReadKeyStroke(gST->ConIn, &Key);

  return Key; // 回傳按鍵結構
}

/**
  取得使用者輸入的字串。
  支援 Backspace 刪除字元，並在螢幕上即時顯示輸入內容。

  @param[in]  Prompt      提示文字，顯示在輸入框之前。
  @param[out] Buffer      指向用於儲存輸入字串的緩衝區。
  @param[in]  BufferSize  緩衝區的大小 (Bytes)，防止溢位。
**/
VOID GetStringInput(IN CHAR16 *Prompt, OUT CHAR16 *Buffer, IN UINTN BufferSize) {
  EFI_INPUT_KEY Key;
  UINTN Count = 0; // 目前已輸入的字元數

  // 設定文字屬性：白色文字，黑色背景
  gST->ConOut->SetAttribute(gST->ConOut, EFI_WHITE | EFI_BACKGROUND_BLACK);
  
  // 印出提示文字 (例如 "Variable Name: ")
  Print(L"%s", Prompt);
  
  // 啟用游標顯示 (True)，讓使用者知道目前輸入位置
  gST->ConOut->EnableCursor(gST->ConOut, TRUE);

  // 初始化輸出緩衝區，將其填滿 0 (NULL)
  ZeroMem(Buffer, BufferSize);

  // 進入無窮迴圈，直到使用者按下 Enter
  while (TRUE) {
    Key = WaitKey(); // 等待按鍵

    // 判斷按鍵類型
    if (Key.UnicodeChar == CHAR_CARRIAGE_RETURN) {
      // 如果是 Enter 鍵 (Carriage Return)，結束輸入
      break;
    } else if (Key.UnicodeChar == CHAR_BACKSPACE) {
      // 如果是 Backspace 鍵 (刪除)
      if (Count > 0) {
        // 在螢幕上模擬刪除：輸出 "倒退鍵" + "空白" + "倒退鍵"
        Print(L"\b \b");
        // 減少計數，並將緩衝區對應位置清空
        Buffer[--Count] = L'\0';
      }
    } else if (Key.UnicodeChar >= 0x20 && Key.UnicodeChar <= 0x7E) {
      // 如果是可列印字元 (ASCII 0x20 到 0x7E)
      // 檢查緩衝區是否還有空間 (保留最後一個 Byte 給 NULL 結尾)
      if (Count < (BufferSize / sizeof(CHAR16)) - 1) {
        // 將字元存入緩衝區
        Buffer[Count++] = Key.UnicodeChar;
        // 將字元顯示在螢幕上 (Echo)
        Print(L"%c", Key.UnicodeChar);
      }
    }
  }

  // 輸入結束，隱藏游標
  gST->ConOut->EnableCursor(gST->ConOut, FALSE);
  // 換行
  Print(L"\n");
}

/**
  GUID 輸入遮罩介面。
  顯示 "________-____-____-____-____________" 格式，讓使用者輸入 Hex 字元。

  @param[out] Guid    指向用於儲存解析後 GUID 的結構。

  @return BOOLEAN     TRUE: 使用者輸入了有效的 GUID。
                      FALSE: 使用者未輸入任何內容直接按 Enter (表示使用預設值)。
**/
BOOLEAN GetGuidInput(OUT EFI_GUID *Guid) {
  EFI_INPUT_KEY Key;
  CHAR16 GuidStr[37];     // 用於儲存 GUID 字串 (36字元 + 1 NULL)
  CHAR16 DisplayStr[37];  // 用於顯示遮罩的字串模板
  UINTN  Index = 0;       // 目前輸入的 Hex 數字計數 (0-31)
  UINTN  ColStart, RowStart; // 紀錄起始游標位置
  
  // 複製遮罩模板到顯示字串
  StrCpyS(DisplayStr, 37, L"________-____-____-____-____________");
  // 清空 GUID 字串緩衝區
  ZeroMem(GuidStr, sizeof(GuidStr));

  // 顯示提示訊息
  Print(L"Enter Vendor GUID (leave empty to use default): ");
  
  // 取得目前游標位置 (Column, Row)
  ColStart = gST->ConOut->Mode->CursorColumn;
  RowStart = gST->ConOut->Mode->CursorRow;

  // 印出遮罩 (底線)
  Print(L"%s", DisplayStr);
  
  // 將游標移回遮罩的開頭，準備讓使用者填空
  gST->ConOut->SetCursorPosition(gST->ConOut, ColStart, RowStart);

  while (TRUE) {
    Key = WaitKey(); // 等待按鍵

    if (Key.UnicodeChar == CHAR_CARRIAGE_RETURN) {
      // 如果按下 Enter
      if (Index == 0) {
        // 如果完全沒有輸入任何數字，表示使用者想用預設值
        Print(L"\nUsing Default GUID.\n");
        return FALSE; 
      }
      // 否則假設輸入完成，跳出迴圈
      break; 
    }
    else if (Key.UnicodeChar == CHAR_BACKSPACE) {
      // 如果按下 Backspace
      if (Index > 0) {
        Index--; // 索引退回上一位
        
        // 特殊處理：如果退回的位置剛好是 '-' 分隔線，需要再多退一位
        // 標準 GUID 格式分隔線位置在第 8, 12, 16, 20 個數字之後
        // 對應到輸入索引 (Index) 為 8, 12, 16, 20 時，游標實際上在 '-' 之後
        if (Index == 8 || Index == 13 || Index == 18 || Index == 23) {
           if (Index > 0) Index--;
        }
        
        // 更新螢幕顯示：
        // 1. 取得當前游標 Column
        UINTN CurrCol = gST->ConOut->Mode->CursorColumn;
        // 2. 游標左移一格
        gST->ConOut->SetCursorPosition(gST->ConOut, CurrCol - 1, RowStart);
        // 3. 印出底線 '_' 覆蓋原本的數字
        Print(L"_");
        // 4. 游標再次左移回底線位置，等待重新輸入
        gST->ConOut->SetCursorPosition(gST->ConOut, CurrCol - 1, RowStart);
      }
    }
    else {
      // 檢查輸入是否為 Hex 合法字元 (0-9, a-f, A-F)
      BOOLEAN IsHex = (Key.UnicodeChar >= L'0' && Key.UnicodeChar <= L'9') || 
                      (Key.UnicodeChar >= L'a' && Key.UnicodeChar <= L'f') || 
                      (Key.UnicodeChar >= L'A' && Key.UnicodeChar <= L'F');
      
      // GUID 總共有 32 個 Hex 數字 (不含分隔線)
      if (IsHex && Index < 32) { 
        // 取得輸入字元
        CHAR16 Char = Key.UnicodeChar;
        // 如果是小寫 (a-z)，轉換為大寫 (A-Z) 以便顯示與處理
        if (Char >= L'a' && Char <= L'z') Char -= 0x20;
        
        // 在螢幕上印出該字元
        Print(L"%c", Char);
        
        // 為了將輸入轉換為 EFI_GUID，我們需要構建一個標準格式的字串 (包含 '-')
        // 格式: XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX
        // 計算當前輸入字元在 GuidStr 緩衝區中的實際位置 (需考慮跳過 '-')
        UINTN RealIndex = Index;
        if (Index >= 8) RealIndex++;  // 跳過第一個 '-'
        if (Index >= 12) RealIndex++; // 跳過第二個 '-'
        if (Index >= 16) RealIndex++; // 跳過第三個 '-'
        if (Index >= 20) RealIndex++; // 跳過第四個 '-'
        
        GuidStr[RealIndex] = Char; // 存入緩衝區
        
        Index++; // 輸入計數 +1
        
        // 如果剛好輸入到分隔線的位置，螢幕上需要自動印出 '-' 並跳過
        // 同時在緩衝區也要補上 '-'
        if (Index == 8 || Index == 12 || Index == 16 || Index == 20) {
           Print(L"-"); // 螢幕輸出
           GuidStr[RealIndex+1] = L'-'; // 緩衝區填入
        }
      }
    }
  }
  
  Print(L"\n");
  
  // 使用 BaseLib 的 StrToGuid 函式，將字串解析為 EFI_GUID 結構
  // 如果格式錯誤，則回傳錯誤並將 GUID 設為 0
  if (StrToGuid(GuidStr, Guid) != EFI_SUCCESS) {
     Print(L"Invalid GUID format, using zero GUID.\n");
     ZeroMem(Guid, sizeof(EFI_GUID));
  }
  return TRUE; // 回傳 TRUE 表示有自定義輸入
}

/**
  顯示變數的詳細資料 (名稱、GUID、大小、Hex Dump)。
  使用綠色文字顯示，模仿駭客/系統工具風格。

  @param[in] Name      變數名稱。
  @param[in] Guid      變數 Vendor GUID。
  @param[in] DataSize  資料大小。
  @param[in] Data      指向資料內容的指標。
**/
VOID PrintVariableData(CHAR16* Name, EFI_GUID *Guid, UINTN DataSize, VOID* Data) {
  // SetAttribute: 設定文字顏色為亮綠色 (EFI_LIGHTGREEN)，背景為黑色 (EFI_BACKGROUND_BLACK)
  gST->ConOut->SetAttribute(gST->ConOut, EFI_LIGHTGREEN | EFI_BACKGROUND_BLACK);
  
  // 印出 Vendor GUID (使用 %g 格式化符號)
  Print(L"Vendor GUID: %g\n", Guid);
  // 印出變數名稱與資料大小
  Print(L"Name: %s Data Size: %d\n", Name, DataSize);
  
  // 如果有資料，進行 Hex Dump (16進位傾印)
  if (DataSize > 0 && Data != NULL) {
      UINT8 *ByteData = (UINT8*)Data; // 轉型為 Byte 指標以便逐 Byte 讀取
      for (UINTN i = 0; i < DataSize; i++) {
        // 印出 2位數的大寫 Hex 值 (例如 0A, FF)
        Print(L"%02X ", ByteData[i]);
        
        // 每輸出 16 個 Byte 換一行，且最後一行不需換行
        if ((i + 1) % 16 == 0 && (i + 1) < DataSize) {
            Print(L"\n");
        }
      }
  }
  // 資料印完後換兩行，作為分隔
  Print(L"\n\n");
  
  // 恢復預設顏色 (白字黑底)
  gST->ConOut->SetAttribute(gST->ConOut, EFI_WHITE | EFI_BACKGROUND_BLACK);
}

//
// 主要功能函式 (Main Functions)
//

/**
  功能 1: 列出系統中所有變數 (List All Variables)。
  此功能會遍歷系統所有變數，存入列表，並以分頁方式顯示。
**/
VOID ListAllVariables() {
  EFI_STATUS Status;
  UINTN VariableNameSize = 512; // 初始變數名稱緩衝區大小
  CHAR16 *VariableName;         // 用於儲存 GetNextVariableName 回傳的名稱
  EFI_GUID VendorGuid;          // 用於儲存 GetNextVariableName 回傳的 GUID
  UINTN DataSize = 0;
  
  VARIABLE_INFO *VarList = NULL; // 動態陣列，儲存所有掃描到的變數資訊
  UINTN VarCount = 0;            // 目前找到的變數數量
  UINTN VarCapacity = 0;         // 動態陣列目前的容量

  // 清除螢幕
  gST->ConOut->ClearScreen(gST->ConOut);
  Print(L"Scanning variables... Please wait.\n");

  // 1. 分配初始緩衝區給 VariableName
  // AllocateZeroPool: 分配記憶體並清零
  VariableName = AllocateZeroPool(VariableNameSize);
  if (VariableName == NULL) return; // 記憶體不足，直接返回
  
  // 根據 UEFI 規範，第一次呼叫 GetNextVariableName 時，VariableName 必須是空字串
  VariableName[0] = L'\0';

  // ---------------------------------------------------------
  // 掃描迴圈：遍歷所有變數
  // ---------------------------------------------------------
  while (TRUE) {
    UINTN OldBufferSize = VariableNameSize;
    
    // gRT->GetNextVariableName: Runtime Service，取得下一個變數名稱與 GUID。
    // 輸入: VariableNameSize (緩衝區大小), VariableName (當前名稱), VendorGuid (當前 GUID)
    // 輸出: VariableName (下一個名稱), VendorGuid (下一個 GUID), Status
    Status = gRT->GetNextVariableName(&VariableNameSize, VariableName, &VendorGuid);

    // 檢查: 緩衝區是否太小? (EFI_BUFFER_TOO_SMALL)
    // 如果變數名稱長度超過了目前的 VariableNameSize，系統會回傳此錯誤，並更新 VariableNameSize 為所需大小。
    if (Status == EFI_BUFFER_TOO_SMALL) {
      // 重新分配更大的記憶體 (ReallocatePool)
      VariableName = ReallocatePool(OldBufferSize, VariableNameSize, VariableName);
      if (VariableName == NULL) {
          // 記憶體不足，無法繼續
          break;
      }
      // 使用新的緩衝區再次嘗試取得該變數
      Status = gRT->GetNextVariableName(&VariableNameSize, VariableName, &VendorGuid);
    }
    
    // 檢查: 是否已遍歷完畢 (EFI_NOT_FOUND)
    if (Status == EFI_NOT_FOUND) {
      break; // 掃描結束，跳出迴圈
    }
    // 檢查: 是否發生其他錯誤
    if (EFI_ERROR(Status)) {
      break; // 發生未知錯誤，停止掃描
    }

    // 取得該變數的 Data Size
    // 呼叫 GetVariable，傳入 Data=NULL, DataSize=0。
    // 目的不是讀取資料，而是讓系統回傳所需的大小 (EFI_BUFFER_TOO_SMALL 並更新 DataSize)。
    DataSize = 0;
    gRT->GetVariable(VariableName, &VendorGuid, NULL, &DataSize, NULL);

    // ---------------------------------------------------------
    // 儲存到動態陣列 (類似 C++ Vector 的擴充邏輯)
    // ---------------------------------------------------------
    if (VarCount >= VarCapacity) {
      // 如果容量不足，將容量翻倍 (初始為 64)
      UINTN NewCapacity = (VarCapacity == 0) ? 64 : VarCapacity * 2;
      // 分配新的陣列
      VARIABLE_INFO *NewList = AllocateZeroPool(NewCapacity * sizeof(VARIABLE_INFO));
      if (NewList == NULL) break;
      
      // 如果舊陣列有資料，複製到新陣列
      if (VarList != NULL) {
        CopyMem(NewList, VarList, VarCount * sizeof(VARIABLE_INFO));
        FreePool(VarList); // 釋放舊陣列
      }
      VarList = NewList;   // 指向新陣列
      VarCapacity = NewCapacity; // 更新容量
    }

    // 儲存當前變數資訊
    // AllocateCopyPool: 分配記憶體並複製字串內容
    VarList[VarCount].Name = AllocateCopyPool(StrSize(VariableName), VariableName);
    // CopyMem: 複製 GUID 結構
    CopyMem(&VarList[VarCount].Guid, &VendorGuid, sizeof(EFI_GUID));
    // 儲存資料大小
    VarList[VarCount].DataSize = DataSize;
    
    VarCount++; // 計數加 1
  }
  
  // 掃描完畢，釋放 VariableName 緩衝區
  FreePool(VariableName);

  // ---------------------------------------------------------
  // 分頁顯示介面
  // ---------------------------------------------------------
  UINTN PageSize = 20; // 每頁顯示 20 筆
  // 計算總頁數: (總數 + 頁大小 - 1) / 頁大小
  UINTN TotalPages = (VarCount + PageSize - 1) / PageSize;
  if (TotalPages == 0) TotalPages = 1; // 至少顯示 1 頁
  UINTN CurrentPage = 1; // 當前頁碼
  EFI_INPUT_KEY Key;

  while (TRUE) {
    gST->ConOut->ClearScreen(gST->ConOut);
    
    // 繪製標題列：設定藍底白字
    gST->ConOut->SetAttribute(gST->ConOut, EFI_WHITE | EFI_BACKGROUND_BLUE);
    // 使用格式化對齊: %-40s (靠左40字元), %-10s (靠左10字元)
    Print(L"%-40s | %-10s | %s\n", L"Variable Name", L"Data Size", L"Vendor GUID");
    // 恢復黑底白字
    gST->ConOut->SetAttribute(gST->ConOut, EFI_WHITE | EFI_BACKGROUND_BLACK);

    // 計算本頁的起始與結束索引
    UINTN StartIndex = (CurrentPage - 1) * PageSize;
    UINTN EndIndex = StartIndex + PageSize;
    if (EndIndex > VarCount) EndIndex = VarCount; // 防止超出總數

    // 迴圈印出本頁的所有變數
    for (UINTN i = StartIndex; i < EndIndex; i++) {
      CHAR16 DisplayName[41];
      ZeroMem(DisplayName, sizeof(DisplayName));
      // StrnCpyS: 安全複製字串，截斷超過 40 字元的部分，避免表格跑版
      StrnCpyS(DisplayName, 41, VarList[i].Name, 40);
      
      // 印出單行資訊
      Print(L"%-40s | %-10d | %g\n", DisplayName, VarList[i].DataSize, &VarList[i].Guid);
    }
    
    // 如果本頁不滿 PageSize，填補空行以保持版面高度固定
    for (UINTN i = 0; i < (PageSize - (EndIndex - StartIndex)); i++) {
        Print(L"\n");
    }

    // 底部狀態列
    Print(L"\nTotal: %d   Page: %d/%d   Showing: %d-%d\n", VarCount, CurrentPage, TotalPages, StartIndex + 1, EndIndex);
    Print(L"Keys: Up/Down  PgUp/PgDn  ESC exit\n");

    // 等待按鍵進行翻頁
    Key = WaitKey();
    if (Key.ScanCode == SCAN_ESC) {
      break; // 離開列表
    } else if (Key.ScanCode == SCAN_PAGE_DOWN || Key.ScanCode == SCAN_DOWN) {
      // 下一頁
      if (CurrentPage < TotalPages) CurrentPage++;
    } else if (Key.ScanCode == SCAN_PAGE_UP || Key.ScanCode == SCAN_UP) {
      // 上一頁
      if (CurrentPage > 1) CurrentPage--;
    }
  }

  // 釋放 VarList 中每個名稱字串的記憶體
  for (UINTN i = 0; i < VarCount; i++) {
    if (VarList[i].Name) FreePool(VarList[i].Name);
  }
  // 釋放 VarList 陣列本身
  if (VarList) FreePool(VarList);
}

/**
  功能 2: 搜尋變數名稱 (Search By Name)。
  支援部分名稱匹配 (Substring Search)，並會掃描所有 GUID。
**/
VOID SearchByName() {
  CHAR16 SearchName[100]; // 儲存使用者輸入的搜尋關鍵字
  
  UINTN NameSize = 512;
  CHAR16 *VariableName;
  EFI_GUID VendorGuid;
  
  UINTN DataSize = 0;
  VOID *DataBuffer = NULL;
  EFI_STATUS Status;
  
  UINTN Count = 0; // 統計找到的符合變數數量
  
  gST->ConOut->ClearScreen(gST->ConOut);
  
  // 1. 取得使用者輸入的關鍵字
  GetStringInput(L"Variable name: ", SearchName, sizeof(SearchName));

  // 2. 準備開始遍歷變數
  VariableName = AllocateZeroPool(NameSize);
  if (VariableName == NULL) return;
  VariableName[0] = L'\0'; // 初始為空

  // 3. 遍歷系統所有變數
  while (TRUE) {
    UINTN OldSize = NameSize;
    // 取得下一個變數
    Status = gRT->GetNextVariableName(&NameSize, VariableName, &VendorGuid);

    // 處理緩衝區不足
    if (Status == EFI_BUFFER_TOO_SMALL) {
      VariableName = ReallocatePool(OldSize, NameSize, VariableName);
      if (VariableName == NULL) {
        Print(L"Memory allocation failed.\n");
        break;
      }
      Status = gRT->GetNextVariableName(&NameSize, VariableName, &VendorGuid);
    }
    
    if (Status == EFI_NOT_FOUND) break; // 結束
    if (EFI_ERROR(Status)) break;       // 錯誤

    // 4. 比對名稱：檢查變數名稱是否 "包含" 搜尋關鍵字
    // StrStr(S1, S2): 在 S1 中尋找 S2。若找到回傳指標，否則回傳 NULL。
    if (StrStr(VariableName, SearchName) != NULL) {
      
      // 匹配成功，準備讀取該變數的資料
      DataSize = 0;
      // 第一次 GetVariable: 取得 DataSize
      Status = gRT->GetVariable(VariableName, &VendorGuid, NULL, &DataSize, NULL);
      
      // 如果 DataSize > 0，分配記憶體並讀取內容
      if (Status == EFI_BUFFER_TOO_SMALL || DataSize > 0) {
         DataBuffer = AllocatePool(DataSize);
         if (DataBuffer != NULL) {
           // 第二次 GetVariable: 實際讀取資料
           Status = gRT->GetVariable(VariableName, &VendorGuid, NULL, &DataSize, DataBuffer);
           if (!EFI_ERROR(Status)) {
             // 呼叫顯示函式印出綠色 Hex Dump
             PrintVariableData(VariableName, &VendorGuid, DataSize, DataBuffer);
             Count++;
           }
           FreePool(DataBuffer); // 釋放資料緩衝區
         }
      } else {
         // 變數存在但無資料內容 (DataSize 為 0)
         PrintVariableData(VariableName, &VendorGuid, 0, NULL);
         Count++;
      }
    }
  }
  
  FreePool(VariableName);

  // 顯示搜尋結果統計
  if (Count == 0) {
     Print(L"Variable '%s' not found.\n", SearchName);
  } else {
     Print(L"Number of variables found: %d\n", Count);
  }
  
  Print(L"Press any key to continue...\n");
  WaitKey();
}

/**
  功能 3: 依據 Vendor GUID 搜尋變數 (Search By GUID)。
  列出所有屬於特定 GUID 的變數。
**/
VOID SearchByGuid() {
  EFI_GUID SearchGuid = gDefaultVendorGuid; // 預設搜尋的 GUID
  BOOLEAN UseCustomGuid;
  EFI_STATUS Status;
  UINTN NameSize = 512;
  CHAR16 *Name;
  EFI_GUID VendorGuid;
  UINTN Count = 0;
  UINTN DataSize;
  VOID *Data;

  gST->ConOut->ClearScreen(gST->ConOut);
  
  // 1. 讓使用者輸入 GUID，若直接按 Enter 則使用預設值
  UseCustomGuid = GetGuidInput(&SearchGuid);
  if (!UseCustomGuid) {
      SearchGuid = gDefaultVendorGuid;
  }

  gST->ConOut->ClearScreen(gST->ConOut);
  
  Name = AllocateZeroPool(NameSize);
  if (Name == NULL) return;
  Name[0] = L'\0';

  // 2. 遍歷所有變數
  while (TRUE) {
    UINTN OldSize = NameSize;
    Status = gRT->GetNextVariableName(&NameSize, Name, &VendorGuid);
    
    if (Status == EFI_BUFFER_TOO_SMALL) {
      Name = ReallocatePool(OldSize, NameSize, Name);
      if (Name == NULL) break;
      Status = gRT->GetNextVariableName(&NameSize, Name, &VendorGuid);
    }
    
    if (Status == EFI_NOT_FOUND) break;

    // 3. 比對 GUID 是否相符
    // CompareGuid: UEFI BaseLib 函式，比較兩個 GUID 是否相同
    if (CompareGuid(&VendorGuid, &SearchGuid)) {
      
      // GUID 相同，取得變數資料
      DataSize = 0;
      gRT->GetVariable(Name, &VendorGuid, NULL, &DataSize, NULL);
      
      if (DataSize > 0) {
         Data = AllocatePool(DataSize);
         if (Data != NULL) {
           gRT->GetVariable(Name, &VendorGuid, NULL, &DataSize, Data);
           // 顯示資料
           PrintVariableData(Name, &VendorGuid, DataSize, Data);
           FreePool(Data);
         }
         Count++;
      }
    }
  }
  FreePool(Name);
  
  if (Count == 0) {
    Print(L"No variables found.\n");
  } else {
    Print(L"Number of variables: %d\n", Count);
  }
  Print(L"Press any key to continue...\n");
  WaitKey();
}

/**
  功能 4: 建立新變數 (Create New Variable)。
  包含屬性選擇選單。
**/
VOID CreateNewVariable() {
  CHAR16 NameBuf[100];      // 變數名稱緩衝區
  CHAR16 DataStr[100];      // 變數內容字串緩衝區
  EFI_GUID TargetGuid = gDefaultVendorGuid;
  UINT32 Attributes = 0;    // 變數屬性 (Bitmask)
  UINTN SelectAttr = 0;     // 選單索引
  EFI_INPUT_KEY Key;
  EFI_STATUS Status;

  // ---------------------------------------------------------
  // 步驟 1: 屬性選擇選單 (UI 互動)
  // ---------------------------------------------------------
  while(TRUE) {
    gST->ConOut->ClearScreen(gST->ConOut);
    // 印出綠底黑字標題
    gST->ConOut->SetAttribute(gST->ConOut, EFI_LIGHTGREEN | EFI_BACKGROUND_BLACK);
    Print(L"Select variable attributes\n");
    gST->ConOut->SetAttribute(gST->ConOut, EFI_WHITE | EFI_BACKGROUND_BLACK);

    // 定義選單選項
    CHAR16 *Options[] = {
      L"BOOTSERVICE_ACCESS",
      L"BOOTSERVICE_ACCESS | RUNTIME_ACCESS",
      L"BOOTSERVICE_ACCESS | NON_VOLATILE",
      L"BOOTSERVICE_ACCESS | RUNTIME_ACCESS | NON_VOLATILE"
    };

    // 繪製選項，選中的項目用藍底反白
    for (UINTN i = 0; i < 4; i++) {
      if (i == SelectAttr) {
          gST->ConOut->SetAttribute(gST->ConOut, EFI_WHITE | EFI_BACKGROUND_BLUE); 
      } else {
          gST->ConOut->SetAttribute(gST->ConOut, EFI_WHITE | EFI_BACKGROUND_BLACK);
      }
      Print(L"%s\n", Options[i]);
    }
    gST->ConOut->SetAttribute(gST->ConOut, EFI_WHITE | EFI_BACKGROUND_BLACK);

    // 處理上下鍵選擇與 Enter 確認
    Key = WaitKey();
    if (Key.ScanCode == SCAN_UP && SelectAttr > 0) {
        SelectAttr--;
    } else if (Key.ScanCode == SCAN_DOWN && SelectAttr < 3) {
        SelectAttr++;
    } else if (Key.UnicodeChar == CHAR_CARRIAGE_RETURN) {
        break; // 選定，跳出迴圈
    }
  }

  // 根據選擇設定對應的 UEFI 屬性 Bitmask
  // EFI_VARIABLE_NON_VOLATILE (NV): 斷電後保存
  // EFI_VARIABLE_BOOTSERVICE_ACCESS (BS): Boot Service 期間可存取
  // EFI_VARIABLE_RUNTIME_ACCESS (RT): OS 執行期間可存取 (必須搭配 BS)
  switch (SelectAttr) {
    case 0: Attributes = EFI_VARIABLE_BOOTSERVICE_ACCESS; break;
    case 1: Attributes = EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_RUNTIME_ACCESS; break;
    case 2: Attributes = EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_NON_VOLATILE; break;
    case 3: Attributes = EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_RUNTIME_ACCESS | EFI_VARIABLE_NON_VOLATILE; break;
  }

  // ---------------------------------------------------------
  // 步驟 2: 輸入變數名稱、GUID、資料
  // ---------------------------------------------------------
  gST->ConOut->ClearScreen(gST->ConOut);
  // 輸入名稱
  GetStringInput(L"New variable name: ", NameBuf, sizeof(NameBuf));
  
  // 輸入 GUID (選用自定義或預設)
  if (GetGuidInput(&TargetGuid) == FALSE) {
      TargetGuid = gDefaultVendorGuid;
  }

  // 輸入內容字串
  GetStringInput(L"Variable Data (String): ", DataStr, sizeof(DataStr));

  // 呼叫 gRT->SetVariable 建立變數
  // 參數 1: NameBuf (名稱)
  // 參數 2: TargetGuid (廠商 GUID)
  // 參數 3: Attributes (屬性 mask)
  // 參數 4: StrSize(DataStr) (資料大小，包含字串結尾 NULL)
  // 參數 5: DataStr (資料內容指標)
  Status = gRT->SetVariable(NameBuf, &TargetGuid, Attributes, StrSize(DataStr), DataStr);
  
  if (EFI_ERROR(Status)) {
      Print(L"Failed: %r\n", Status); // 失敗，印出錯誤碼
  } else {
      Print(L"Variable Created!\n");  // 成功
  }
  
  WaitKey(); // 暫停查看結果
}

/**
  功能 5: 刪除變數 (Delete Variable)。
  原理是呼叫 SetVariable 並將 DataSize 設為 0。
**/
VOID DeleteVariable() {
  CHAR16 NameBuf[100];
  EFI_GUID TargetGuid = gDefaultVendorGuid;
  EFI_STATUS Status;

  gST->ConOut->ClearScreen(gST->ConOut);
  // 印出藍色標題
  gST->ConOut->SetAttribute(gST->ConOut, EFI_LIGHTBLUE | EFI_BACKGROUND_BLACK);
  Print(L"Delete variable\n");
  gST->ConOut->SetAttribute(gST->ConOut, EFI_WHITE | EFI_BACKGROUND_BLACK);

  // 輸入欲刪除的變數名稱
  GetStringInput(L"Variable name to delete: ", NameBuf, sizeof(NameBuf));
  
  // 輸入該變數的 GUID
  if (GetGuidInput(&TargetGuid) == FALSE) {
      TargetGuid = gDefaultVendorGuid;
  }

  if (NameBuf[0] != L'\0') {
    // 呼叫 gRT->SetVariable 進行刪除
    // DataSize = 0 且 Data = NULL 即代表刪除操作
    // Attributes 在刪除時通常可以忽略 (設為0)，但在某些嚴格的系統上可能需要匹配原屬性
    Status = gRT->SetVariable(NameBuf, &TargetGuid, 0, 0, NULL);
    
    if (!EFI_ERROR(Status)) {
      Print(L"Variable Deleted.\n");
    } else {
      // 常見錯誤: EFI_NOT_FOUND (變數不存在), EFI_WRITE_PROTECTED (唯讀變數)
      Print(L"Error: %r\n", Status);
    }
  }
  WaitKey();
}

/**
  應用程式入口點 (Entry Point)。
  UEFI Loader 載入此程式後會從這裡開始執行。

  @param[in] ImageHandle    此應用程式映像檔的 Handle。
  @param[in] SystemTable    指向 UEFI System Table 的指標，包含系統服務 (BS, RT, ConIn, ConOut)。

  @return EFI_STATUS        執行結果 (通常回傳 EFI_SUCCESS)。
**/
EFI_STATUS EFIAPI UefiMain(IN EFI_HANDLE ImageHandle, IN EFI_SYSTEM_TABLE *SystemTable) {
  UINTN Index = 0; // 主選單當前選擇的索引
  EFI_INPUT_KEY Key;
  
  // 主選單文字陣列
  CHAR16 *Menu[] = {
    L"List all variables",            // 索引 0
    L"Search variables by name",      // 索引 1
    L"Search variables by vendor GUID",// 索引 2
    L"Create new variable",           // 索引 3
    L"Delete variable",               // 索引 4
    L"Exit"                           // 索引 5
  };

  // 隱藏游標，美化選單介面
  gST->ConOut->EnableCursor(gST->ConOut, FALSE);

  // 主迴圈
  while (TRUE) {
    gST->ConOut->ClearScreen(gST->ConOut); // 清除畫面
    
    // 顯示綠色標題資訊
    gST->ConOut->SetAttribute(gST->ConOut, EFI_LIGHTGREEN | EFI_BACKGROUND_BLACK);
    Print(L"Default Vendor GUID: %g\nVariable Application\n", &gDefaultVendorGuid);
    
    // 繪製選單項目
    for (UINTN i = 0; i < 6; i++) {
      if (i == Index) {
        // 如果是當前選中的項目，使用藍底白字 (HighLight)
        gST->ConOut->SetAttribute(gST->ConOut, EFI_WHITE | EFI_BACKGROUND_BLUE);
      } else {
        // 其他項目使用黑底白字
        gST->ConOut->SetAttribute(gST->ConOut, EFI_WHITE | EFI_BACKGROUND_BLACK);
      }
      Print(L"%s\n", Menu[i]);
    }
    // 恢復預設顏色
    gST->ConOut->SetAttribute(gST->ConOut, EFI_WHITE | EFI_BACKGROUND_BLACK);

    // 等待使用者操作
    Key = WaitKey();
    
    // 處理方向鍵
    if (Key.ScanCode == SCAN_UP && Index > 0) {
      Index--; // 上移
    } else if (Key.ScanCode == SCAN_DOWN && Index < 5) {
      Index++; // 下移
    } else if (Key.UnicodeChar == CHAR_CARRIAGE_RETURN) {
      // 按下 Enter，根據索引執行對應功能
      switch (Index) {
        case 0: ListAllVariables(); break;
        case 1: SearchByName(); break;
        case 2: SearchByGuid(); break;
        case 3: CreateNewVariable(); break;
        case 4: DeleteVariable(); break;
        case 5: return EFI_SUCCESS; // 退出程式
      }
    }
  }
  return EFI_SUCCESS;
}