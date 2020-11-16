dest_dir = build
src_dir = src
bin_dir = $(dest_dir)/bin
obj_dir = $(dest_dir)/objects
depends_dir = $(dest_dir)/depends
	
CC = gcc
LD = gcc
CFLAGS = -g -Wall -Werror -O3 -std=c99 -pie -fPIC
LDFLAGS = 
LIBS = -lpthread -lseccomp

vpath %.c  src/argtable:src/container:src/logger:src/rules:src
vpath %.h  src/argtable:src/container:src/logger:src/rules:src

CFILES = main.c util.c parser.c argtable3.c c_cpp.c general.c container.c logger.c
ofiles = $(CFILES:%.c=$(obj_dir)/%.o)

program = $(bin_dir)/sandbox
$(program): $(ofiles)

$(bin_dir)/%:
	@echo ">>> Linking" $@ "<<<"
	@if [ ! -d $(bin_dir) ]; then mkdir -p $(bin_dir); fi;
	$(LD) $(LDFLAGS) $^ -o $@ $(LIBS)

$(obj_dir)/%.o: %.c
	@echo ">>> Compiling" $< "<<<"
	@if [ ! -d $(obj_dir) ]; then mkdir -p $(obj_dir); fi;
	$(CC) $(CFLAGS) -c -o $@ $< $(LIBS)

# $(obj_dir)/%.o: $(src_dir)/*/%.c
# 	@echo ">>> Compiling" $< "<<<"
# 	@if [ ! -d $(obj_dir) ]; then mkdir -p $(obj_dir); fi;
# 	$(CC) $(CFLAGS) -c -o $@ $< $(LIBS)

c_dfiles = $(CFILES:%.c=$(depends_dir)/%.d)

dfiles = $(c_dfiles)

$(depends_dir)/%.d: %.c
	@echo ">>> Building dependency file for" $< "<<<"
	@if [ ! -d $(depends_dir) ]; then mkdir -p $(depends_dir); fi;
	@$(SHELL) -ec '$(CC) -MM $(CFLAGS) $< \
	| sed '\''s@$*.o[ ]*:@$(depends_dir)/$(notdir $@) $(obj_dir)/&@g'\'' > $@'

include $(dfiles)

.PHONY: install clean
install:
	ln -sf $(shell pwd)/$(program) /usr/bin/sandbox
clean:
	# rm -f `find $(dest_dir) -type f -print | egrep -v '(CVS|cvsignore)'`
	rm -rf $(dest_dir)
	rm -f sandbox.log
	rm -f /usr/bin/sandbox
