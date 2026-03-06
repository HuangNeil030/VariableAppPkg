# VariableAppPkg

---

# UEFI 變數應用程式開發筆記 (README)

## 1. 核心變數服務 (UEFI Runtime Services)

這些函數位於 `gRT` (Runtime Services) 表中，用於操作 UEFI 環境變數 (NVRAM)。

### 1.1 `GetVariable` - 讀取變數

**功能**：獲取指定變數的值 (Data) 或其大小 (DataSize)。

* **原型**：
```c
EFI_STATUS GetVariable (
  IN CHAR16     *VariableName,
  IN EFI_GUID   *VendorGuid,
  OUT UINT32    *Attributes OPTIONAL,
  IN OUT UINTN  *DataSize,
  OUT VOID      *Data OPTIONAL
);

```

* **參數重點**：
* `VariableName`：變數名稱字串 (NULL 結尾)。
* `VendorGuid`：廠商唯一識別碼。
* `DataSize`：**輸入**時表示 Buffer 大小；**輸出**時表示變數實際資料大小。
* `Data`：用於存放資料的緩衝區。


* **關鍵用法 (技巧)**：
1. **取得資料大小 (探路)**：傳入 `Data = NULL` 且 `DataSize = 0`。若變數存在，會回傳 `EFI_BUFFER_TOO_SMALL`，並將實際大小填入 `DataSize` 。

2. **讀取資料**：根據取得的大小 `AllocatePool` 分配記憶體，再呼叫一次 `GetVariable` 讀取內容。

* **程式碼範例**：
```c
// 第一次呼叫：取得大小
DataSize = 0;
Status = gRT->GetVariable(Name, &Guid, NULL, &DataSize, NULL);
if (Status == EFI_BUFFER_TOO_SMALL) {
    // 分配記憶體並第二次呼叫
    Data = AllocatePool(DataSize);
    gRT->GetVariable(Name, &Guid, NULL, &DataSize, Data);
}

```

### 1.2 `GetNextVariableName` - 遍歷變數

**功能**：列舉系統中所有的變數名稱與 GUID。

* **原型**：
```c
EFI_STATUS GetNextVariableName (
  IN OUT UINTN     *VariableNameSize,
  IN OUT CHAR16    *VariableName,
  IN OUT EFI_GUID  *VendorGuid
);

```

* **參數重點**：
* `VariableNameSize`：輸入緩衝區大小，若太小會被更新為所需大小。
* `VariableName`：**輸入**為上一個變數名稱 (或空字串開始)；**輸出**為下一個變數名稱 。

* `VendorGuid`：輸出對應的 GUID。

* **關鍵用法 (技巧)**：
1. **開始遍歷**：第一次呼叫時，`VariableName` 的第一個字元必須設為 `L'\0'` (空字串) 。

2. **處理緩衝區不足**：若回傳 `EFI_BUFFER_TOO_SMALL`，需 `ReallocatePool` 擴大緩衝區並**重試** 。

3. **結束條件**：當回傳 `EFI_NOT_FOUND` 時表示遍歷結束 。

* **程式碼範例**：
```c
VariableName[0] = L'\0'; // 初始為空
while (TRUE) {
    Status = gRT->GetNextVariableName(&Size, Name, &Guid);
    if (Status == EFI_NOT_FOUND) break;
    // ... 處理變數 ...
}

```

### 1.3 `SetVariable` - 建立/修改/刪除變數

**功能**：寫入變數資料。若資料大小為 0，則視為刪除。

* **原型**：
```c
EFI_STATUS SetVariable (
  IN CHAR16    *VariableName,
  IN EFI_GUID  *VendorGuid,
  IN UINT32    Attributes,
  IN UINTN     DataSize,
  IN VOID      *Data
);

```


* **參數重點**：
* `Attributes`：屬性位元遮罩 (如 `NON_VOLATILE`, `BOOTSERVICE_ACCESS`) 。

* `DataSize`：資料大小。**設為 0 代表刪除變數** 。

* **屬性 (Attributes)**：
* `0x00000001` (NV): 非揮發性 (斷電保存) 。

* `0x00000002` (BS): Boot Services 期間可存取 。

* `0x00000004` (RT): Runtime 期間可存取 (必須搭配 BS) 。

* **關鍵用法 (技巧)**：
1. **刪除變數**：`SetVariable(Name, Guid, 0, 0, NULL)` 。


2. **建立變數**：必須指定正確的 `Attributes`，否則可能建立失敗或無法保存。

---

## 2. 輸出入服務 (UEFI Boot Services & Console)

用於實作 UI 介面，如選單顯示、顏色控制與按鍵讀取。

### 2.1 `gST->ConOut->SetAttribute` - 設定顏色

**功能**：設定文字的前景與背景顏色。

* **參數**：`Attribute` (1 Byte)。
* 低 4 位元 (0-3)：文字顏色 (Foreground) 。


* 高 3 位元 (4-6)：背景顏色 (Background) 。

* **常用顏色定義**：
* `EFI_WHITE` (0x0F), `EFI_BLACK` (0x00), `EFI_LIGHTGREEN` (0x0A), `EFI_BLUE` (0x01) 。

* `EFI_BACKGROUND_BLUE` (0x10), `EFI_BACKGROUND_BLACK` (0x00) 。

* **應用**：
* `SetAttribute(gST->ConOut, EFI_WHITE | EFI_BACKGROUND_BLUE)`：製作藍底白字的選單 Highlight 效果。

### 2.2 `gST->ConOut->ClearScreen` - 清除螢幕

**功能**：清除螢幕內容並將游標移回 (0,0) 。

* **注意**：清除後的背景色取決於最後一次 `SetAttribute` 設定的背景色。

### 2.3 `gBS->WaitForEvent` & `gST->ConIn->ReadKeyStroke` - 讀取按鍵

**功能**：實作 `WaitKey` 函數，暫停程式直到使用者按鍵。

* **WaitForEvent**：
* 參數 `NumberOfEvents`: 1。
* 參數 `Event`: `gST->ConIn->WaitForKey` (等待鍵盤輸入事件) 。


* 功能：阻塞程式執行，直到鍵盤緩衝區有資料。


* **ReadKeyStroke**：
* 參數 `Key`: `EFI_INPUT_KEY` 結構 (包含 `ScanCode` 與 `UnicodeChar`) 。


* 功能：從緩衝區取出按鍵資料。


* **按鍵判斷**：
* `ScanCode`: 處理特殊鍵 (如 `SCAN_UP`, `SCAN_DOWN`, `SCAN_ESC`) 。

* `UnicodeChar`: 處理字元鍵 (如 'a', '1', Enter `CHAR_CARRIAGE_RETURN`) 。

---

## 3. 輔助函式庫 (Library Functions)

在 `VariableApp.c` 中常用到的標準函式庫功能。

* **`AllocatePool` / `AllocateZeroPool**`: 動態分配記憶體 (類似 `malloc`)，用於儲存變數名稱或內容。
* **`FreePool`**: 釋放記憶體 (類似 `free`)。
* **`CopyMem`**: 複製記憶體內容 (類似 `memcpy`)，用於備份變數列表。
* **`StrCmp` / `StrStr**`: 字串比較與搜尋。`StrStr` 用於實作關鍵字搜尋功能。
* **`StrToGuid`**: 將字串 (如 "12345678-...") 解析為 `EFI_GUID` 結構，用於自定義 GUID 輸入。
* **`Print`**: 格式化輸出到螢幕。
* `%s`: Unicode 字串。
* `%g`: GUID 格式 (EDKII 特有)。
* `%02X`: 2位數 Hex (用於 Hex Dump)。

---

## 4. 程式邏輯流程圖 (簡易版)

**主程式 (UefiMain)**

1. 初始化：隱藏游標 `EnableCursor(FALSE)`。
2. 無窮迴圈 `while(TRUE)`：
* `ClearScreen` 清畫面。
* 繪製選單 (使用 `SetAttribute` 高亮當前選項)。
* `WaitKey` 等待按鍵。
* 根據按鍵更新索引 (`Index`) 或執行功能 (`switch-case`)。



**功能：List All Variables**

1. `GetNextVariableName` 迴圈掃描所有變數。
2. 將變數資訊 (`Name`, `Guid`, `Size`) 存入動態陣列 (`VarList`)。
3. 進入分頁迴圈：
* 計算 `StartIndex` 與 `EndIndex`。
* 印出該頁變數 (`Print` 配合 `%-40s` 對齊)。
* 等待翻頁按鍵 (`PgUp`/`PgDn`)。



**功能：Search by Name**

1. 輸入關鍵字。
2. `GetNextVariableName` 迴圈掃描。
3. 使用 `StrStr` 檢查名稱是否包含關鍵字。
4. 若符合，呼叫 `GetVariable` 讀取資料並 `PrintVariableData` (綠色 Hex Dump)。

這是一份關於此 UEFI 變數應用程式的**系統架構與主選單流程**的詳細筆記。這份說明以教科書的結構編排，幫助您從宏觀角度理解程式的運作原理。

---

# 系統架構與主選單流程 (System Architecture & Main Menu Flow)

## 一、 系統架構 (System Architecture)

此 UEFI 應用程式並非直接操作硬體，而是透過 UEFI 規範所提供的標準介面（層級化架構）來達成目的。整個系統可以分為三個主要層級：

```text
+-------------------------------------------------------------+
|                     1. 應用程式層 (Application)             |
|                       (VariableApp.efi)                     |
|  負責：使用者介面(UI)、商業邏輯(選單、分頁、搜尋字串比對)   |
+------------------------------+------------------------------+
                               | 呼叫 (Call)
                               V
+------------------------------+------------------------------+
|               2. UEFI 系統服務與協定層 (UEFI Services)      |
|                                                             |
| +--------------------+ +------------------+ +-------------+ |
| |  Runtime Services  | |  Boot Services   | | Protocols   | |
| |       (gRT)        | |      (gBS)       | |   (gST)     | |
| |--------------------| |------------------| |-------------| |
| | GetVariable        | | WaitForEvent     | | ConIn       | |
| | GetNextVariableName| | AllocatePool     | | ConOut      | |
| | SetVariable        | | FreePool         | |             | |
| +--------------------+ +------------------+ +-------------+ |
+------------------------------+------------------------------+
                               | 存取 (Access)
                               V
+------------------------------+------------------------------+
|                     3. 韌體與硬體層 (Hardware/Firmware)     |
|   NVRAM (Non-Volatile RAM) / SPI Flash / 系統記憶體         |
|   負責：實際儲存 UEFI 變數資料                              |
+-------------------------------------------------------------+

```

### 架構層級說明：

1. **應用程式層 (Application)**：這是我們撰寫的 `VariableApp.c`。它負責接收使用者的按鍵、組織畫面排版，並決定何時該呼叫底層 API。
2. **UEFI 系統服務與協定層 (UEFI Services)**：
* 
**Runtime Services (`gRT`)**：提供在作業系統(OS)載入前與載入後都能使用的服務 。在此程式中，負責所有變數的核心操作（讀取、遍歷、寫入、刪除）。


* 
**Boot Services (`gBS`)**：僅在開機階段（呼叫 `ExitBootServices` 前）可用的服務 。負責記憶體分配 (`AllocatePool`) 以及等待按鍵事件 (`WaitForEvent`) 。


* 
**System Table (`gST`)**：包含標準輸入/輸出協定。`gST->ConOut` 負責控制螢幕（清空螢幕、設定顏色、印出文字），`gST->ConIn` 負責讀取鍵盤輸入 。




3. 
**韌體與硬體層 (Hardware)**：變數實際上存放在主機板的 NVRAM 晶片中 。當我們呼叫 `gRT->SetVariable` 且屬性包含 `EFI_VARIABLE_NON_VOLATILE` 時，UEFI 韌體會將資料寫入此實體晶片中，使其在斷電後仍能保存 。



---

## 二、 主選單流程 (Main Menu Flow)

程式的進入點是 `UefiMain` 函式。它的核心架構是一個**無限迴圈 (Infinite Loop)**，這種設計模式在早期作業系統或嵌入式系統介面中非常常見，通常被稱為「事件驅動迴圈 (Event Loop)」。

### 流程圖 (Flowchart)

```text
[程式啟動 UefiMain]
       |
       V
 (初始化) 隱藏游標 EnableCursor(FALSE)
       |
       V
+---> [清空螢幕 ClearScreen]
|      |
|      V
|   [繪製標題與預設 GUID]
|      |
|      V
|   [繪製選單項目] 
|    (使用迴圈檢查 i == Index，若相符則用藍底白字高亮顯示)
|      |
|      V
|   [等待按鍵 WaitKey] (程式在此阻塞，直到使用者按下按鍵)
|      |
|      V
|   [判斷按鍵 ScanCode / UnicodeChar]
|      |
|      +--- 若按 "上 (UP)" ---> 檢查 Index > 0 ---> Index = Index - 1 -----+
|      |                                                                   |
|      +--- 若按 "下 (DOWN)" -> 檢查 Index < 5 ---> Index = Index + 1 -----+
|      |                                                                   |
|      +--- 若按 "Enter" --------------------------------------------------+
|                                                                          |
|       (進入 Switch 執行對應功能)                                         |
|       |-- Case 0: 呼叫 ListAllVariables()                                |
|       |-- Case 1: 呼叫 SearchByName()                                    |
|       |-- Case 2: 呼叫 SearchByGuid()                                    |
|       |-- Case 3: 呼叫 CreateNewVariable()                               |
|       |-- Case 4: 呼叫 DeleteVariable()                                  |
|       |-- Case 5: return EFI_SUCCESS (退出程式) --------------------[程式結束]
|                                                                          |
+---------------------- (功能執行完畢，返回主迴圈) <-------------------------+

```

### 流程步驟詳解：

1. **狀態記錄 (`Index` 變數)**：
程式使用一個整數變數 `Index` 來記錄目前使用者「選中」了哪一個選項。選單有 6 個項目，所以 `Index` 的有效範圍是 `0` 到 `5`。
2. **畫面重繪 (Render)**：
每次迴圈開始，第一件事就是 `ClearScreen()` 洗掉舊畫面 。接著利用 `for` 迴圈印出選單。這裡利用了簡單的邏輯：如果當前印出的項目編號 `i` 剛好等於 `Index`，就呼叫 `SetAttribute` 將顏色改為藍底白字 ，否則維持黑底白字。這創造了「游標移動」的視覺效果。


3. **事件阻塞 (Blocking Wait)**：
畫面畫好後，呼叫 `WaitKey()`。此時程式會「停住 (Block)」，不佔用 CPU 資源，直到鍵盤發出中斷（使用者按下按鍵）。


4. **狀態更新與分發 (State Update & Dispatch)**：
收到按鍵後，程式判斷是什麼鍵。如果是方向鍵，只負責**更新狀態 (`Index`)**，接著迴圈回到步驟 2，畫面就會因為 `Index` 改變而將高亮標籤畫在不同位置。如果是 Enter 鍵，則透過 `switch(Index)` 進入對應的子系統功能。
5. **子系統返回**：
當 `ListAllVariables()` 或 `SearchByName()` 等函式執行完畢（通常是使用者在子畫面按了任何鍵後 `return`），程式碼會回到 `UefiMain` 的 `switch` 區塊結束處，接著迴圈繼續，重新清空螢幕並畫出主選單。

---

cd /d D:\BIOS\MyWorkSpace\edk2

edksetup.bat Rebuild

chcp 65001

set PYTHONUTF8=1

set PYTHONIOENCODING=utf-8

rmdir /s /q Build\VariableAppPkg

build -p VariableAppPkg\VariableAppPkg.dsc -a X64 -t VS2019 -b DEBUG
