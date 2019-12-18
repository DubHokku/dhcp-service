# dhcp-service

Тестовая версия DHCP сервиса в составе openFlow контроллера Runos ( https://github.com/ARCCN/runos ), выполняет обрабоку запросов DISCOVER и REQUEST, отвечает пакетами OFFER и ACK. 


Зависимости:

используется docker контейнер с образом alpine linux runos/runos-2.0, http://arccn.github.io/runos/docs-2.0/eng/11_RUNOS_InstallationGuide.html#installation-with-docker


Сборка:

    $ uname -r
    4.19.3-200.fc28.x86_64
    
    $ docker run -i -t -P --name runosdoc runos/runos-2.0
    
    $ cp dhcp-service /home/runos/src/apps
    $ cd /home/runos/build

    $ nix-shell
    $ cmake ..
    $ make

Запуск:

    $ cd /home/runos
    # build/runos

Проверка:

интерфейс для прослушивания определяется в dhcp_service.h

    # define NIC "enp3s0" 

настройка зоны в runos::dhcp_service::init(), dhcp_service.cc
