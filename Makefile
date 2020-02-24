CC = gcc
CFLAGS = -g -Wall

dest_dir = build
obj_dir = $(dest_dir)/objects
bin_dir = $(dest_dir)/bin

CFILES = main.c
c_ofiles = $(CFILES:%.c=$(obj_dir)/%.o)

ofiles = $(c_ofiles)

program = $(bin_dir)/main
$(program): $(ofiles)

$(bin_dir)/%:
	@echo ">>> Linking" $@ "<<<"
	$(CC) $^ -o $@
	ln -sf $@ $(notdir $@)

$(obj_dir)/%.o: %.c
	@echo ">>> Compiling" $< "<<<"
	$(CC) $(CFLAGS) -c -o $@ $<

.PHONY: clean
clean:
	rm -f `find $(dest_dir) -type f -print | egrep -v '(CVS|cvsignore)'`
	rm -f main