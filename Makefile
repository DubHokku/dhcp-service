TARGET = arccn
SETPATH = /usr/local/bin
.PHONY: all clean install uninstall


# -- make obj
all: $(TARGET)
service.o: src/dhcp_service.cc
	g++ -c -std=c++11 -ltins -o service.o src/dhcp_service.cc

main.o: src/$(TARGET).cc
	g++ -c -std=c++11 -o main.o src/$(TARGET).cc


# -- make bin
$(TARGET): service.o main.o
	g++ -ltins -o $(TARGET) main.o service.o



# -- make install
clean:
	rm -rf $(TARGET) main.o servie.o client.o

install:
	install $(TARGET) $(SETPATH)

uninstall:
	rm -rf $(SETPATH)/$(TARGET)
