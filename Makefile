CXX=g++ -fPIC
CXXFLAGS=-I/opt/homebrew/opt/openssl@1.1/include -I/opt/homebrew/include -I/usr/local/opt/openssl@1.1/include -I/opt/local/include -I/usr/local/include -std=c++20 -g -DHAVE_INTTYPES_H -DHAVE_NETINET_IN_H -Wall -Werror
LDFLAGS=-L/opt/homebrew/opt/openssl@1.1/lib -L/opt/homebrew/lib -L/usr/local/opt/openssl@1.1/lib -L/opt/local/lib -L/usr/local/lib -lphosg -levent -levent-async -lssl -lcrypto -levent_openssl -g -std=c++20 -lstdc++
OBJECTS=FieldTypes.o AirtableClient.o

ifeq ($(shell uname -s),Darwin)
	INSTALL_DIR=/opt/local
	CXXFLAGS +=  -DMACOSX -mmacosx-version-min=10.15
else
	INSTALL_DIR=/usr/local
	CXXFLAGS +=  -DLINUX
endif

all: libairtable.a airtable

install: install-cli install-lib

install-cli: airtable
	cp airtable $(INSTALL_DIR)/bin/

install-lib: libairtable.a
	mkdir -p $(INSTALL_DIR)/include/airtable
	cp -r *.hh $(INSTALL_DIR)/include/airtable/
	cp libairtable.a $(INSTALL_DIR)/lib/

libairtable.a: $(OBJECTS)
	rm -f libairtable.a
	ar rcs libairtable.a $^

airtable: $(OBJECTS) AirtableCLI.o
	g++ $(LDFLAGS) -o airtable $^

clean:
	rm -rf *.dSYM *.o gmon.out libairtable.a airtable

.PHONY: clean
