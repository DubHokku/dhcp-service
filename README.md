# dhcp-service

Тестовая версия DHCP сервиса в составе openFlow контроллера RuNOS ( https://github.com/ARCCN/runos ), выполняет обрабоку запросов DISCOVER и REQUEST, отвечает пакетами OFFER и ACK. Приём и передача данных выполняется с применением OpenFlow 1.3.


Зависимости:

libtins ( https://libtins.github.io/ ), libfluid ( http://opennetworkingfoundation.github.io/libfluid )

используется docker контейнер runos/runos-2.0
( http://arccn.github.io/runos/docs-2.0/eng/11_RUNOS_InstallationGuide.html#installation-with-docker )


Сборка:

    $ uname -r
    4.19.3-200.fc28.x86_64
    
    $ docker run -i -t -P --name runosdoc runos/runos-2.0
    
    $ cp dhcp-service /home/runos/src/apps
    $ cd /home/runos
    $ nix-shell
    
    $ cd /home/runos/build
    $ cmake ..
    $ make

Запуск:

    $ cd /home/runos
    # build/runos

    
Проверка:

с применением эмулятора mininet ( https://github.com/mininet/mininet )

    # mn --topo linear,4 --switch ovsk,protocols=OpenFlow13 --controller remote,ip=172.17.0.4,port=6653
    
    mininet> h1 ifconfig
    
    mininet> h1 dhclient -r
    mininet> h1 dhclient -1
    
    mininet> h1 ifconfig

интерфейс для прослушивания определяется в dhcp_service.hh

    # define NIC "eth0" 

настройка зоны в runos::dhcp_service::pool()
