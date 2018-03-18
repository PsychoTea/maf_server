TARGET   = maf
OUTDIR  ?= bin

CC       = xcrun -sdk iphoneos gcc -arch arm64
LDID     = ldid
CHMOD    = chmod

DEV_IP   = 192.168.1.120
MAF_PATH = /psycho/maf

all: $(OUTDIR)/$(TARGET)

$(OUTDIR):
	mkdir -p $(OUTDIR)

$(OUTDIR)/$(TARGET): src/*.c src/lib/*.a | $(OUTDIR)
	$(CC) -framework Foundation -o $@ $^
	$(LDID) -Sentitlements.xml $@
	$(CHMOD) 755 $@

install:
	make
	ssh root@$(DEV_IP) "rm $(MAF_PATH)"
	scp bin/maf root@$(DEV_IP):$(MAF_PATH)

clean:
	rm -rf $(OUTDIR)
