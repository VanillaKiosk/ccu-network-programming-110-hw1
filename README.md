# ccu-network-programming-110-hw1

## Run and test the program

1. 執行以下命令以執行 server
```
git clone https://github.com/VanillaKiosk/ccu-network-programming-110-hw1.git
```
```
cd ccu-network-programming-110-hw1
```
```
make
```
```
./server ./www ./www/upload
```

2. 打開瀏覽器輸入 http://localhost:57080/index.html

3. 點選頁面上的紫色下一頁按鈕切換到有表單頁面即可測試上傳功能（有設計可一次上傳多個檔案哦，真的是塞在同一個表單送出，不是分兩次connection）。

上傳的檔案會放在 ./www/upload （可在命令參數更改上傳路徑哦～）

要下載剛上傳的檔案只要開啟 http://localhost:57080/upload/XXX(檔名) 即可。
