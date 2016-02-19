# Copyright 2013-16 Board of Trustees of Stanford University
# Copyright 2013-16 Ecole Polytechnique Federale Lausanne (EPFL)
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
# THE SOFTWARE.

SUBDIRS = dp libix apps
CLEANDIRS = $(SUBDIRS:%=clean-%)

all: $(SUBDIRS)

apps: libix

$(SUBDIRS):
	$(MAKE) -C $@

clean: $(CLEANDIRS)

style:
	astyle -A8 -T8 -p -U -H --suffix=~ -r -Q --exclude=deps --exclude=inc/lwip --exclude=dp/lwip --exclude=dp/net/tcp.c --exclude=dp/net/tcp_in.c --exclude=dp/net/tcp_out.c '*.c' '*.h'

style-check:
	$(eval TEST=$(shell astyle --dry-run --formatted -A8 -T8 -p -U -H --suffix=~ -r -Q --exclude=deps --exclude=inc/lwip --exclude=dp/lwip --exclude=dp/net/tcp.c --exclude=dp/net/tcp_in.c --exclude=dp/net/tcp_out.c '*.c' '*.h' | grep Formatted))
	@if [ -z "$(TEST)" ] ; then\
	    echo "success";\
	    exit 0;\
	else\
	    echo "failed : $(TEST)";\
	    exit 1;\
	fi

$(CLEANDIRS):
	$(MAKE) -C $(@:clean-%=%) clean

.PHONY: all clean style $(SUBDIRS) $(CLEANDIRS)
