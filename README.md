# dhcp-service

Тестовая версия DHCP сервиса. Выполняет обрабоку запросов DISCOVER и REQUEST, отвечает пакетами OFFER и ACK. 

Зависимости:

используются вызовы библиотеки libtins, https://libtins.github.io

Сборка:

    $ uname -r
    4.19.3-200.fc28.x86_64

    $ make

Запуск:

    # ./arccn

Проверка:

интерфейс для прослушивания определяется в dhcp_service.h

    # define NIC "enp3s0" 

настройка зоны в arccn.cc
