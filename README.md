# SP17-B05902109
National Taiwan University Computer Science Department
2017 Fall System Programming

## MP0 - 字數統計

```bash
make
./char_count [string] [fileName]
```

- 計算 [string] 內字元在 [fileName] 內的數量
- 保證
  - 可以一分鐘內完成1G文字檔
  - 若檔案開啟失敗，輸出 error

## MP1 - 使用 MD5 進行版本控制
```bash
make
./loser status <目錄>
./loser commit <目錄>
./loser log <數量> <目錄>
```
- 版本維護系統，根據命令不同有不同功能

- loser status

  ```bash
  loser status <目錄>
  ```

  `loser status` 是最簡單也最常用的子指令，它能夠追蹤並顯示所求目錄相較於**上一次 commit （不需考慮更久之前的 commit）** 產生了哪些變化，我們將這些變化分類爲：

  1. new_file： 檔名在上一次 commit 沒有出現過，並且不符合copied的條件。
  2. modified： 檔名在上一次 commit 中出現過，但 MD5 的結果與上次不同。
  3. copied： 檔名在上一次 commit 沒有出現，但 MD5 的結果與上次 commit 中的某個檔案相同。
  4. deleted : 檔名在上一次 commit 中出現過，但 當前目錄中已不存在。

  具體的輸出格式其實就是一個 commit 記錄去掉各項 size 與 MD5 部分：

  ```bash
  [new_file]
  <新檔案檔名1>
  <新檔案檔名2>
  .
  .
  [modified]
  <被修改檔案檔名1>
  <被修改檔案檔名2>
  .
  .
  [copied]
  <原檔名1> => <新檔名1>
  <原檔名2> => <新檔名2>
  .
  .
  [deleted]
  <被刪除檔案檔名1>
  <被刪除檔案檔名2>
  ```

  我們特別來看一下[copied]：

  ```bash
  [copied]
  <原檔名1> => <新檔名1>
  <原檔名2> => <新檔名2>
  .
  .
  ```

  很可能會發生一個檔案的 MD5 與上一次 commit 中不只一個檔案的 MD5 相同的情形，那此時的<原檔名>請使用字典順序最小者。

  同樣記得[new_file]、[modified]、[copied] 以下都會跟隨一行行字串，每一行都該**遵循字典順序由小到大排序**。

  - 特殊情形

    - .loser_record 檔案不存在：視所有檔案爲新檔案

    - 檔案與上一次 commit 沒有任何不同：輸出

      ```
        [new_file]
        [modified]
        [copied]
        [deleted]
      ```

- loser commit

  ```bash
  loser commit <目錄>
  ```

  每次執行會計算與上次 commit 的差異並追加到 .loser_record 檔案的末尾。

  格式見 [.loser_record](https://systemprogrammingatntu.github.io/mp1/#.loser_record) 一節。

  MD5 部分則記錄**目錄底下的所有檔案**（.loser_record 除外）與其 MD5 的對應。

  - 特殊情形

    - .loser_record 檔案不存在、但存在其他檔案：建立 .loser_record 檔案（權限爲該檔案擁有者可讀可寫），並視所有檔案爲新檔案

- loser log

  ```
  loser log <數量> <目錄>
  ```

  log 子指令會接一個 <數量> ，表示該輸出 .loser_record 檔案中倒數 <數量> 個 commit 資訊。 這些資訊與 .loser_record 檔案記錄的資訊完全相同，但順序相反，換句話說，越新的 commit 越先輸出。

  <數量>保證爲一個數字

  具體輸出格式則類似 status，只是須註明為第幾次commit，每筆 commit 之間需空格一行。

  ```bash
  # commit n
  [new_file]
  <新檔案檔名1>
  <新檔案檔名2>
  .
  .
  [modified]
  <被修改檔案檔名1>
  <被修改檔案檔名2>
  .
  .
  [copied]
  <原檔名1> => <新檔名1>
  <原檔名2> => <新檔名2>
  .
  .
  [deleted]
  <被刪除檔案檔名1>
  <被刪除檔案檔名2>
  .
  .
  (MD5)
  <檔名> <md5>
  .
  .
  
  # commit n-1
  .
  .
  .
  ```

  - 特殊情形
    - .loser_record 檔案不存在（.loser_record 只要存在就至少包含一個 commit） ：不輸出任何東西
    - `loser log <數量> <目錄>` 的數量大於目前 commit 的數量：輸出所有歷史

## MP2 - Syncronize file between client and server

以下複製於2018年網站，可能與2017年作業有所不同

- CSIE box

  - CSIE box is a client-server service, with one client and one server (you can extend it to support multiple clients and get bouns). Server and client will both monitor a directory. The files in both side should always be same. The server and client communicate with each other through FIFO. Both should handle that FIFO may be accidently broken.

- Required Features
  - Server
    - Server will monitor a directory that already exist. Then, it will create FIFOs, and wait for client to connect.
    - Server should not crash when client is disconnected.
  - Client
    - Client will monitor an empty directory. If it is not empty, remove all content.
    - Before connect to the Server, the monitored directory’s permission should set to 000 for preventing anyone to write it.
    - Set to 700 after synced.
    - Client will be terminate by SIGINT (ctrl+c). After the client receive this signal, client should remove the monitored directory.
    - Client should not crash when server is disconnected.
- File hierarchy and usage

  - Place a Makefile under our homework directory. The judge will run make to build a csie_box_server and csie_box_client executable in the same directory.

- We will run the server by ./csie_box_server [CONFIG_FILE]. Your program should loads the specified config file with this example content.

  ```C
  fifo_path = /home/fifo_dir
  directory = /home/resol/dir
  ```

- According the the config, it creates two fifos, `/home/fifo_dir/server_to_client.fifo` and `/home/fifo_dir/client_to_server.fifo` on startup, and monitory the files in `/home/resol/dir` directory.

- We will run the client by `./csie_box_client [CONFIG_FILE]`. The config file’s format is same as the server.

## MP3 - Simple Mining (multiple process)

- 執行 server 和 client，由 server 通過 pipeline 發送命令給 client

- 目標：使用雜湊函數md5，將隨機字串轉換成md5格式後，根據prefix的0的數量決定寶藏珍貴度

  - 隨機字串開頭 b05902109 為起始
  - 假設 b05902109abc 為 0???????????? 則為 1-treasure
  - 前綴不改變，持續計算直到發生 b05902109abcaaaa為 00??????????，則為 2-treasure
  - 若提前發現 (n+1)-treasure 但未發現 (n)-treasure，則忽略

- server

  ```bash
  make
  ./boss [config]
  ```

  - config, pipeline name

  ```typescript
  MINE: mine.bin
  MINER: ../a_in ../a_out
  MINER: ../b_in ../b_out
  MINER: ../c_in ../c_out
  ```

- client

  ```bash
  make
  ./miner [miner_name] [input_pipe] [output_pipe]
  ```


## MP4 - Online Chatroom
## MP5 - Daemon Process