INSTALL = install
bindir	=  /usr/bin

can-test:	can-test.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -std=gnu99 -Wall -W $(LDFLAGS) $< -o $@

install:	can-test
	$(INSTALL) -p -m 0755 can-test $(DESTDIR)$(bindir)
