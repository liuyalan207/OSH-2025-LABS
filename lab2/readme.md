#### c语言实现shell部分功能

因为对c++不够熟练，所以把makefile和shell.c都改成了c语言的版本

### 功能实现
pwd和cd

<img width="117" alt="捕获pwd" src="https://github.com/user-attachments/assets/f8a0948d-a8cc-449b-9caa-afec4b14ae93" />

管道

<img width="226" alt="捕获管道" src="https://github.com/user-attachments/assets/6dffa0d1-945f-4dc8-bbc7-b83a04de8112" />

重定向：注意处理管道 | 符号和重定向符号的优先级

<img width="212" alt="捕获dx" src="https://github.com/user-attachments/assets/9bdd3fda-a496-42d8-a1c1-5fb6ee8ea148" />

信号处理：注意函数内不要出现多余的释放内存空间的操作，功能：中断输入，中断嵌套，中断指令运行

<img width="158" alt="捕获sl" src="https://github.com/user-attachments/assets/025ab08c-519d-4792-b07d-6d3f7873a2d6" />

前后台进程 fg,pg的实现

<img width="233" alt="捕获fg" src="https://github.com/user-attachments/assets/52478486-c887-4dd1-a7bf-5a20476319e4" />
