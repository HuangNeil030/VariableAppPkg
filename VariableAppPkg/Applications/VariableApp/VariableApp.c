/** @file
  Variable Application to List, Search, Create and Delete UEFI Variables.
**/

#include <Uefi.h>
#include <Library/UefiLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/BaseLib.h>
#include <Library/PrintLib.h>

//
// 全域變數：預設 Vendor GUID
//
EFI_GUID gDefaultVendorGuid = { 0x37B93825, 0x3B85, 0x02D0, { 0x37, 0xB9, 0x33, 0xF9, 0x00, 0x00, 0x00, 0x00 } };

// 結構：用於儲存變數列表以便分頁
typedef struct {
  CHAR16   *Name;
  EFI_GUID Guid;
  UINTN    DataSize;
} VARIABLE_INFO;

//
// 輔助函式 (Helper Functions)
//

// 等待按鍵輸入
EFI_INPUT_KEY WaitKey() {
  UINTN Index;
  EFI_INPUT_KEY Key;
  gBS->WaitForEvent(1, &gST->ConIn->WaitForKey, &Index);
  gST->ConIn->ReadKeyStroke(gST->ConIn, &Key);
  return Key;
}

// 取得字串輸入
// Prompt: 提示文字
// Buffer: 接收輸入的緩衝區
// BufferSize: 緩衝區大小
VOID GetStringInput(IN CHAR16 *Prompt, OUT CHAR16 *Buffer, IN UINTN BufferSize) {
  EFI_INPUT_KEY Key;
  UINTN Count = 0;
  
  // 設定為白字黑底
  gST->ConOut->SetAttribute(gST->ConOut, EFI_WHITE | EFI_BACKGROUND_BLACK);
  Print(L"%s", Prompt);
  gST->ConOut->EnableCursor(gST->ConOut, TRUE);

  ZeroMem(Buffer, BufferSize);

  while (TRUE) {
    Key = WaitKey();
    if (Key.UnicodeChar == CHAR_CARRIAGE_RETURN) {
      break;
    } else if (Key.UnicodeChar == CHAR_BACKSPACE) {
      if (Count > 0) {
        Print(L"\b \b");
        Buffer[--Count] = L'\0';
      }
    } else if (Key.UnicodeChar >= 0x20 && Key.UnicodeChar <= 0x7E) {
      if (Count < (BufferSize / sizeof(CHAR16)) - 1) {
        Buffer[Count++] = Key.UnicodeChar;
        Print(L"%c", Key.UnicodeChar);
      }
    }
  }
  gST->ConOut->EnableCursor(gST->ConOut, FALSE);
  Print(L"\n");
}

// GUID 輸入遮罩介面
// 回傳: TRUE=使用者輸入了 GUID, FALSE=使用者直接按 Enter (使用預設)
BOOLEAN GetGuidInput(OUT EFI_GUID *Guid) {
  EFI_INPUT_KEY Key;
  CHAR16 GuidStr[37]; 
  CHAR16 DisplayStr[37];
  UINTN  Index = 0;
  UINTN  ColStart, RowStart;
  
  StrCpyS(DisplayStr, 37, L"________-____-____-____-____________");
  ZeroMem(GuidStr, sizeof(GuidStr));

  Print(L"Enter Vendor GUID (leave empty to use default): ");
  ColStart = gST->ConOut->Mode->CursorColumn;
  RowStart = gST->ConOut->Mode->CursorRow;

  Print(L"%s", DisplayStr);
  gST->ConOut->SetCursorPosition(gST->ConOut, ColStart, RowStart);

  while (TRUE) {
    Key = WaitKey();

    if (Key.UnicodeChar == CHAR_CARRIAGE_RETURN) {
      if (Index == 0) {
        Print(L"\nUsing Default GUID.\n");
        return FALSE; 
      }
      // 假設輸入完成
      break; 
    }
    else if (Key.UnicodeChar == CHAR_BACKSPACE) {
      if (Index > 0) {
        Index--;
        // 跳過 Dash (從後往前退)
        if (Index == 8 || Index == 13 || Index == 18 || Index == 23) {
           if (Index > 0) Index--;
        }
        
        // 更新顯示：退格 -> 印底線 -> 退格
        UINTN CurrCol = gST->ConOut->Mode->CursorColumn;
        gST->ConOut->SetCursorPosition(gST->ConOut, CurrCol - 1, RowStart);
        Print(L"_");
        gST->ConOut->SetCursorPosition(gST->ConOut, CurrCol - 1, RowStart);
      }
    }
    else {
      // 檢查是否為 Hex 字元 (0-9, a-f, A-F)
      BOOLEAN IsHex = (Key.UnicodeChar >= L'0' && Key.UnicodeChar <= L'9') || 
                      (Key.UnicodeChar >= L'a' && Key.UnicodeChar <= L'f') || 
                      (Key.UnicodeChar >= L'A' && Key.UnicodeChar <= L'F');
      
      if (IsHex && Index < 32) { // GUID 共有 32 個 Hex 數字
        // 取得字元並轉大寫顯示
        CHAR16 Char = Key.UnicodeChar;
        if (Char >= L'a' && Char <= L'z') Char -= 0x20;
        
        Print(L"%c", Char);
        
        // 填入 GuidStr Buffer
        UINTN RealIndex = Index;
        if (Index >= 8) RealIndex++;
        if (Index >= 12) RealIndex++;
        if (Index >= 16) RealIndex++;
        if (Index >= 20) RealIndex++;
        
        GuidStr[RealIndex] = Char;
        
        Index++;
        
        // 自動補 Dash
        if (Index == 8 || Index == 12 || Index == 16 || Index == 20) {
           Print(L"-");
           GuidStr[RealIndex+1] = L'-';
        }
      }
    }
  }
  
  Print(L"\n");
  
  // 嘗試解析字串為 GUID
  if (StrToGuid(GuidStr, Guid) != EFI_SUCCESS) {
     Print(L"Invalid GUID format, using zero GUID.\n");
     ZeroMem(Guid, sizeof(EFI_GUID));
  }
  return TRUE; 
}

// 顯示綠色 Hex Dump (仿照截圖格式)
VOID PrintVariableData(CHAR16* Name, EFI_GUID *Guid, UINTN DataSize, VOID* Data) {
  // 設定為綠色文字，黑底
  gST->ConOut->SetAttribute(gST->ConOut, EFI_LIGHTGREEN | EFI_BACKGROUND_BLACK);
  
  Print(L"Vendor GUID: %g\n", Guid);
  Print(L"Name: %s Data Size: %d\n", Name, DataSize);
  
  // 列印 Hex Dump
  if (DataSize > 0 && Data != NULL) {
      UINT8 *ByteData = (UINT8*)Data;
      for (UINTN i = 0; i < DataSize; i++) {
        Print(L"%02X ", ByteData[i]);
        // 每 16 個 Byte 換行
        if ((i + 1) % 16 == 0 && (i + 1) < DataSize) Print(L"\n");
      }
  }
  Print(L"\n\n");
  
  // 恢復預設顏色
  gST->ConOut->SetAttribute(gST->ConOut, EFI_WHITE | EFI_BACKGROUND_BLACK);
}

//
// 主要功能函式 (Main Functions)
//

// 1. List All Variables (列表顯示，含分頁)
VOID ListAllVariables() {
  EFI_STATUS Status;
  UINTN VariableNameSize = 512;
  CHAR16 *VariableName;
  EFI_GUID VendorGuid;
  UINTN DataSize = 0;
  
  VARIABLE_INFO *VarList = NULL;
  UINTN VarCount = 0;
  UINTN VarCapacity = 0;

  gST->ConOut->ClearScreen(gST->ConOut);
  Print(L"Scanning variables... Please wait.\n");

  VariableName = AllocateZeroPool(VariableNameSize);
  if (VariableName == NULL) return;
  VariableName[0] = L'\0';

  // 1. 掃描所有變數並存入動態陣列
  while (TRUE) {
    UINTN OldBufferSize = VariableNameSize;
    Status = gRT->GetNextVariableName(&VariableNameSize, VariableName, &VendorGuid);

    // 處理 Buffer 太小的情況
    if (Status == EFI_BUFFER_TOO_SMALL) {
      VariableName = ReallocatePool(OldBufferSize, VariableNameSize, VariableName);
      if (VariableName == NULL) break;
      Status = gRT->GetNextVariableName(&VariableNameSize, VariableName, &VendorGuid);
    }
    
    if (Status == EFI_NOT_FOUND) break;
    if (EFI_ERROR(Status)) break;

    // 取得 Data Size (傳入 NULL Buffer)
    DataSize = 0;
    gRT->GetVariable(VariableName, &VendorGuid, NULL, &DataSize, NULL);

    // 擴充列表容量
    if (VarCount >= VarCapacity) {
      UINTN NewCapacity = (VarCapacity == 0) ? 64 : VarCapacity * 2;
      VARIABLE_INFO *NewList = AllocateZeroPool(NewCapacity * sizeof(VARIABLE_INFO));
      if (NewList == NULL) break;
      if (VarList != NULL) {
        CopyMem(NewList, VarList, VarCount * sizeof(VARIABLE_INFO));
        FreePool(VarList);
      }
      VarList = NewList;
      VarCapacity = NewCapacity;
    }

    // 儲存變數資訊
    VarList[VarCount].Name = AllocateCopyPool(StrSize(VariableName), VariableName);
    CopyMem(&VarList[VarCount].Guid, &VendorGuid, sizeof(EFI_GUID));
    VarList[VarCount].DataSize = DataSize;
    VarCount++;
  }
  FreePool(VariableName);

  // 2. 顯示分頁介面
  UINTN PageSize = 20;
  UINTN TotalPages = (VarCount + PageSize - 1) / PageSize;
  if (TotalPages == 0) TotalPages = 1;
  UINTN CurrentPage = 1;
  EFI_INPUT_KEY Key;

  while (TRUE) {
    gST->ConOut->ClearScreen(gST->ConOut);
    
    // 繪製藍色標題列
    gST->ConOut->SetAttribute(gST->ConOut, EFI_WHITE | EFI_BACKGROUND_BLUE);
    Print(L"%-40s | %-10s | %s\n", L"Variable Name", L"Data Size", L"Vendor GUID");
    gST->ConOut->SetAttribute(gST->ConOut, EFI_WHITE | EFI_BACKGROUND_BLACK);

    UINTN StartIndex = (CurrentPage - 1) * PageSize;
    UINTN EndIndex = StartIndex + PageSize;
    if (EndIndex > VarCount) EndIndex = VarCount;

    // 列印本頁變數
    for (UINTN i = StartIndex; i < EndIndex; i++) {
      CHAR16 DisplayName[41];
      ZeroMem(DisplayName, sizeof(DisplayName));
      // 截斷過長的名稱以避免表格跑版
      StrnCpyS(DisplayName, 41, VarList[i].Name, 40);
      Print(L"%-40s | %-10d | %g\n", DisplayName, VarList[i].DataSize, &VarList[i].Guid);
    }
    
    // 填補空行以保持版面固定
    for (UINTN i = 0; i < (PageSize - (EndIndex - StartIndex)); i++) Print(L"\n");

    // 底部資訊列
    Print(L"\nTotal: %d   Page: %d/%d   Showing: %d-%d\n", VarCount, CurrentPage, TotalPages, StartIndex + 1, EndIndex);
    Print(L"Keys: Up/Down  PgUp/PgDn  ESC exit\n");

    Key = WaitKey();
    if (Key.ScanCode == SCAN_ESC) {
      break;
    } else if (Key.ScanCode == SCAN_PAGE_DOWN || Key.ScanCode == SCAN_DOWN) {
      if (CurrentPage < TotalPages) CurrentPage++;
    } else if (Key.ScanCode == SCAN_PAGE_UP || Key.ScanCode == SCAN_UP) {
      if (CurrentPage > 1) CurrentPage--;
    }
  }

  // 釋放記憶體
  for (UINTN i = 0; i < VarCount; i++) {
    if (VarList[i].Name) FreePool(VarList[i].Name);
  }
  if (VarList) FreePool(VarList);
}

// 2. Search by Name (搜尋變數名稱 - 掃描所有 GUID 並支援部分匹配)
VOID SearchByName() {
  CHAR16 SearchName[100];
  
  UINTN NameSize = 512;
  CHAR16 *VariableName;
  EFI_GUID VendorGuid;
  
  UINTN DataSize = 0;
  VOID *DataBuffer = NULL;
  EFI_STATUS Status;
  
  UINTN Count = 0; // 統計找到的數量
  
  gST->ConOut->ClearScreen(gST->ConOut);
  
  // 輸入欲搜尋的變數名稱
  GetStringInput(L"Variable name: ", SearchName, sizeof(SearchName));

  // 準備遍歷
  VariableName = AllocateZeroPool(NameSize);
  if (VariableName == NULL) return;
  VariableName[0] = L'\0'; 

  // 遍歷所有變數
  while (TRUE) {
    UINTN OldSize = NameSize;
    Status = gRT->GetNextVariableName(&NameSize, VariableName, &VendorGuid);

    if (Status == EFI_BUFFER_TOO_SMALL) {
      VariableName = ReallocatePool(OldSize, NameSize, VariableName);
      if (VariableName == NULL) {
        Print(L"Memory allocation failed.\n");
        break;
      }
      Status = gRT->GetNextVariableName(&NameSize, VariableName, &VendorGuid);
    }
    
    if (Status == EFI_NOT_FOUND) break;
    if (EFI_ERROR(Status)) break;

    // 使用 StrStr 進行部份匹配 (Substring Match)
    // 例如輸入 "BootOrder" 可以找到 "DefaultBootOrder" 和 "BootOrder"
    if (StrStr(VariableName, SearchName) != NULL) {
      
      // 找到變數，取得資料
      DataSize = 0;
      Status = gRT->GetVariable(VariableName, &VendorGuid, NULL, &DataSize, NULL);
      
      if (Status == EFI_BUFFER_TOO_SMALL || DataSize > 0) {
         DataBuffer = AllocatePool(DataSize);
         if (DataBuffer != NULL) {
           Status = gRT->GetVariable(VariableName, &VendorGuid, NULL, &DataSize, DataBuffer);
           if (!EFI_ERROR(Status)) {
             // 顯示資料
             PrintVariableData(VariableName, &VendorGuid, DataSize, DataBuffer);
             Count++;
           }
           FreePool(DataBuffer);
         }
      } else {
         // 無資料的變數
         PrintVariableData(VariableName, &VendorGuid, 0, NULL);
         Count++;
      }
    }
  }
  
  FreePool(VariableName);

  if (Count == 0) {
     Print(L"Variable '%s' not found.\n", SearchName);
  } else {
     Print(L"Number of variables found: %d\n", Count);
  }
  
  Print(L"Press any key to continue...\n");
  WaitKey();
}

// 3. Search by GUID (搜尋 GUID)
VOID SearchByGuid() {
  EFI_GUID SearchGuid = gDefaultVendorGuid;
  BOOLEAN UseCustomGuid;
  EFI_STATUS Status;
  UINTN NameSize = 512;
  CHAR16 *Name;
  EFI_GUID VendorGuid;
  UINTN Count = 0;
  UINTN DataSize;
  VOID *Data;

  gST->ConOut->ClearScreen(gST->ConOut);
  
  // 使用遮罩介面輸入 GUID
  UseCustomGuid = GetGuidInput(&SearchGuid);
  if (!UseCustomGuid) SearchGuid = gDefaultVendorGuid;

  gST->ConOut->ClearScreen(gST->ConOut);
  
  Name = AllocateZeroPool(NameSize);
  if (Name == NULL) return;
  Name[0] = L'\0';

  // 遍歷所有變數並比對 GUID
  while (TRUE) {
    UINTN OldSize = NameSize;
    Status = gRT->GetNextVariableName(&NameSize, Name, &VendorGuid);
    
    if (Status == EFI_BUFFER_TOO_SMALL) {
      Name = ReallocatePool(OldSize, NameSize, Name);
      if (Name == NULL) break;
      Status = gRT->GetNextVariableName(&NameSize, Name, &VendorGuid);
    }
    
    if (Status == EFI_NOT_FOUND) break;

    // 比對 GUID 是否相符
    if (CompareGuid(&VendorGuid, &SearchGuid)) {
      DataSize = 0;
      gRT->GetVariable(Name, &VendorGuid, NULL, &DataSize, NULL);
      
      // 讀取並顯示資料
      if (DataSize > 0) {
         Data = AllocatePool(DataSize);
         if (Data != NULL) {
           gRT->GetVariable(Name, &VendorGuid, NULL, &DataSize, Data);
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

// 4. Create Variable (建立變數)
VOID CreateNewVariable() {
  CHAR16 NameBuf[100];
  CHAR16 DataStr[100];
  EFI_GUID TargetGuid = gDefaultVendorGuid;
  UINT32 Attributes = 0;
  UINTN SelectAttr = 0;
  EFI_INPUT_KEY Key;
  EFI_STATUS Status;

  // 步驟 1: 屬性選擇選單 (綠色標題)
  while(TRUE) {
    gST->ConOut->ClearScreen(gST->ConOut);
    // 綠底黑字標題
    gST->ConOut->SetAttribute(gST->ConOut, EFI_LIGHTGREEN | EFI_BACKGROUND_BLACK);
    Print(L"Select variable attributes\n");
    gST->ConOut->SetAttribute(gST->ConOut, EFI_WHITE | EFI_BACKGROUND_BLACK);

    CHAR16 *Options[] = {
      L"BOOTSERVICE_ACCESS",
      L"BOOTSERVICE_ACCESS | RUNTIME_ACCESS",
      L"BOOTSERVICE_ACCESS | NON_VOLATILE",
      L"BOOTSERVICE_ACCESS | RUNTIME_ACCESS | NON_VOLATILE"
    };

    for (UINTN i = 0; i < 4; i++) {
      if (i == SelectAttr) gST->ConOut->SetAttribute(gST->ConOut, EFI_WHITE | EFI_BACKGROUND_BLUE); // 藍底反白選中
      else gST->ConOut->SetAttribute(gST->ConOut, EFI_WHITE | EFI_BACKGROUND_BLACK);
      Print(L"%s\n", Options[i]);
    }
    gST->ConOut->SetAttribute(gST->ConOut, EFI_WHITE | EFI_BACKGROUND_BLACK);

    Key = WaitKey();
    if (Key.ScanCode == SCAN_UP && SelectAttr > 0) SelectAttr--;
    else if (Key.ScanCode == SCAN_DOWN && SelectAttr < 3) SelectAttr++;
    else if (Key.UnicodeChar == CHAR_CARRIAGE_RETURN) break;
  }

  // 設定屬性值
  switch (SelectAttr) {
    case 0: Attributes = EFI_VARIABLE_BOOTSERVICE_ACCESS; break;
    case 1: Attributes = EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_RUNTIME_ACCESS; break;
    case 2: Attributes = EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_NON_VOLATILE; break;
    case 3: Attributes = EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_RUNTIME_ACCESS | EFI_VARIABLE_NON_VOLATILE; break;
  }

  // 步驟 2: 輸入變數資訊
  gST->ConOut->ClearScreen(gST->ConOut);
  GetStringInput(L"New variable name: ", NameBuf, sizeof(NameBuf));
  
  if (GetGuidInput(&TargetGuid) == FALSE) TargetGuid = gDefaultVendorGuid;

  GetStringInput(L"Variable Data (String): ", DataStr, sizeof(DataStr));

  // 寫入變數
  Status = gRT->SetVariable(NameBuf, &TargetGuid, Attributes, StrSize(DataStr), DataStr);
  
  if (EFI_ERROR(Status)) Print(L"Failed: %r\n", Status);
  else Print(L"Variable Created!\n");
  
  WaitKey();
}

// 5. Delete Variable (刪除變數)
VOID DeleteVariable() {
  CHAR16 NameBuf[100];
  EFI_GUID TargetGuid = gDefaultVendorGuid;
  EFI_STATUS Status;

  gST->ConOut->ClearScreen(gST->ConOut);
  // 藍色標題
  gST->ConOut->SetAttribute(gST->ConOut, EFI_LIGHTBLUE | EFI_BACKGROUND_BLACK);
  Print(L"Delete variable\n");
  gST->ConOut->SetAttribute(gST->ConOut, EFI_WHITE | EFI_BACKGROUND_BLACK);

  GetStringInput(L"Variable name to delete: ", NameBuf, sizeof(NameBuf));
  if (GetGuidInput(&TargetGuid) == FALSE) TargetGuid = gDefaultVendorGuid;

  if (NameBuf[0] != L'\0') {
    // DataSize = 0 即為刪除
    Status = gRT->SetVariable(NameBuf, &TargetGuid, 0, 0, NULL);
    if (!EFI_ERROR(Status)) Print(L"Variable Deleted.\n");
    else Print(L"Error: %r\n", Status);
  }
  WaitKey();
}

//
// 應用程式入口 (Entry Point)
//
EFI_STATUS EFIAPI UefiMain(IN EFI_HANDLE ImageHandle, IN EFI_SYSTEM_TABLE *SystemTable) {
  UINTN Index = 0;
  EFI_INPUT_KEY Key;
  CHAR16 *Menu[] = {
    L"List all variables",
    L"Search variables by name",
    L"Search variables by vendor GUID",
    L"Create new variable",
    L"Delete variable",
    L"Exit"
  };

  gST->ConOut->EnableCursor(gST->ConOut, FALSE);

  while (TRUE) {
    gST->ConOut->ClearScreen(gST->ConOut);
    // 綠色標題
    gST->ConOut->SetAttribute(gST->ConOut, EFI_LIGHTGREEN | EFI_BACKGROUND_BLACK);
    Print(L"Default Vendor GUID: %g\nVariable Application\n", &gDefaultVendorGuid);
    
    // 繪製選單
    for (UINTN i = 0; i < 6; i++) {
      if (i == Index) gST->ConOut->SetAttribute(gST->ConOut, EFI_WHITE | EFI_BACKGROUND_BLUE);
      else gST->ConOut->SetAttribute(gST->ConOut, EFI_WHITE | EFI_BACKGROUND_BLACK);
      Print(L"%s\n", Menu[i]);
    }
    gST->ConOut->SetAttribute(gST->ConOut, EFI_WHITE | EFI_BACKGROUND_BLACK);

    Key = WaitKey();
    if (Key.ScanCode == SCAN_UP && Index > 0) Index--;
    else if (Key.ScanCode == SCAN_DOWN && Index < 5) Index++;
    else if (Key.UnicodeChar == CHAR_CARRIAGE_RETURN) {
      switch (Index) {
        case 0: ListAllVariables(); break;
        case 1: SearchByName(); break;
        case 2: SearchByGuid(); break;
        case 3: CreateNewVariable(); break;
        case 4: DeleteVariable(); break;
        case 5: return EFI_SUCCESS;
      }
    }
  }
  return EFI_SUCCESS;
}