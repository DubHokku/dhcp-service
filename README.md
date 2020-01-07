# dhcp-service

Тестовая версия DHCP сервиса ( RFC 2131 ), приложение к п.о. openFlow контроллера RuNOS ( https://github.com/ARCCN/runos ), выполняет обрабоку сообщений DISCOVER и REQUEST ( - DECLINE, RELEASE, INFORM ), отвечает -- OFFER и ACK ( NAK ).


Зависимости:

используется docker контейнер runos/runos-2.0


Сборка:

    $ uname -r
    4.19.3-200.fc28.x86_64
    
    $ docker run -i -t -P --name runosdock runos/runos-2.0
    
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

с эмулятором mininet ( https://github.com/mininet/mininet )

    # mn --topo linear,4 --switch ovsk,protocols=OpenFlow13 --controller remote,ip=172.17.0.4,port=6653
    
    mininet> h1 ifconfig
    
    mininet> h1 dhclient -r
    mininet> h1 dhclient -1
    
    mininet> h1 ifconfig

интерфейс определяется в dhcp_service.hpp

    # define NIC "eth0" 

настройка зоны -- runos::dhcp_service::pool()
