# OnlineDocumentDB
Searching lectures on the Internet is always a trouble.
In some labs and groups, we could build a small database to easily keep the paper and get the wanted one.
This project is the database said above !
There are many useful skills and techs has been applied such as multi-thread, heart-beat socket etc. 
Maybe it is not good enough, but I still work on it. 
And I also expect for your support and suggestion !
Notice: Please take care the Copyright of the articles !

To compile, such as:
    icpc *** -lmysqlclient -L/usr/lib64/mysql -I/usr/include/mysql -std=c++11

To use the class ServerDDB, such as:
    ServerDDB<ServerTask> yourServer( yourPort );
And the Server will run by itself !

For more function please check the  .h in this project!

2019.7  Han.  at ShangHai.
