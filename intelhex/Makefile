#
# The MIT License (MIT)
#
# Copyright (c) 2014 Billy Millare
#
# Permission is hereby granted, free of charge, to any person obtaining a copy of
# this software and associated documentation files (the "Software"), to deal in
# the Software without restriction, including without limitation the rights to
# use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
# the Software, and to permit persons to whom the Software is furnished to do so,
# subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in all
# copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
# FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
# COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
# IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
# CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
#
# 18jun2014
#

HEX_SAMPLES=sample/hex
BIN_SAMPLES=sample/bin

CC=gcc
CFLAGS=-Wall -DINTELHEX_STANDALONE -DINTELHEX_VERBOSE
SOURCES=intelhex.c
HEADERS=intelhex.h
BIN=intelhex

SILENT=1> /dev/null
TEMP=/dev/null

default build: $(BIN)
	
$(BIN): $(SOURCES) $(HEADERS)
	$(CC) $(CFLAGS) -o $(BIN) $(SOURCES)

clean:
	rm -f $(BIN)
	rm -rf sample
	
test: test_parameters test_bin test_hex test_conversion
	
setup:
	@tar xfz sample.tar.gz
	
test_parameters: build setup
	@echo
	### testing program parameters...
	
	@echo
	### missing input file
	-./$(BIN) -hex suchanuglynameforafilenoonewantstouse -hex $(TEMP) $(SILENT)
	
	@echo
	### wrong input hex file
	-./$(BIN) -hex $(BIN_SAMPLES)/good.bin -hex $(TEMP) $(SILENT)
	
	@echo
	### wrong input bin file
	-./$(BIN) -bin $(HEX_SAMPLES)/good.hex -bin $(TEMP) $(SILENT)
	
	@echo
	### wrong parameters
	-./$(BIN) -hex $(HEX_SAMPLES)/good.hex -bin $(SILENT)
	-./$(BIN) hex $(HEX_SAMPLES)/good.hex -bin $(TEMP) $(SILENT)
	-./$(BIN) -hex $(HEX_SAMPLES)/good.hex -bin $(TEMP) -invalid $(SILENT)
	-./$(BIN) -hex $(HEX_SAMPLES)/good.hex -bin $(TEMP) -rl256 $(SILENT)
	-./$(BIN) -hex $(HEX_SAMPLES)/good.hex -bin $(TEMP) -rl025 $(SILENT)
	-./$(BIN) -hex $(HEX_SAMPLES)/good.hex -bin $(TEMP) -rl-1 $(SILENT)
	-./$(BIN) -hex $(HEX_SAMPLES)/good.hex -bin $(TEMP) -rl2f5 $(SILENT)
	-./$(BIN) -hex $(HEX_SAMPLES)/good.hex -bin $(TEMP) -ad1 $(SILENT)
	-./$(BIN) -hex $(HEX_SAMPLES)/good.hex -bin $(TEMP) -ad1024 $(SILENT)

test_bin: build setup
	@echo
	### testing bin files...

	@echo
	### invalid CS and IP
	-./$(BIN) -bin $(BIN_SAMPLES)/invalidCSandIP_1.bin -hex $(TEMP) $(SILENT)
	-./$(BIN) -bin $(BIN_SAMPLES)/invalidCSandIP_2.bin -hex $(TEMP) $(SILENT)
	
	@echo
	### invalid base address
	-./$(BIN) -bin $(BIN_SAMPLES)/invalidBaseAddress.bin -hex $(TEMP) $(SILENT)
	
	@echo
	### invalid data size
	-./$(BIN) -bin $(BIN_SAMPLES)/invalidDataSize.bin -hex $(TEMP) $(SILENT)
	
	@echo
	### insufficient data bytes
	-./$(BIN) -bin $(BIN_SAMPLES)/insufficientDataBytes_1.bin -hex $(TEMP) $(SILENT)
	-./$(BIN) -bin $(BIN_SAMPLES)/insufficientDataBytes_2.bin -hex $(TEMP) $(SILENT)
	
	@echo
	### memory overlap
	-./$(BIN) -bin $(BIN_SAMPLES)/memoryOverlap.bin -hex $(TEMP) $(SILENT)
	
	@echo
	### exceeded endmost address
	-./$(BIN) -bin $(BIN_SAMPLES)/exceededEndmostAddress.bin -hex $(TEMP) $(SILENT)
	-./$(BIN) -bin $(BIN_SAMPLES)/exceededEndmostAddress.bin -hex $(TEMP) -ad32 $(SILENT)
	-./$(BIN) -bin $(BIN_SAMPLES)/exceededEndmostAddress.bin -hex $(TEMP) -ad16 $(SILENT)
	-./$(BIN) -bin $(BIN_SAMPLES)/exceededEndmostAddress.bin -hex $(TEMP) -ad8 $(SILENT)
	
	@echo
	### good
	./$(BIN) -bin $(BIN_SAMPLES)/good8.bin -hex $(TEMP) -rl4 -ad8 $(SILENT)
	./$(BIN) -bin $(BIN_SAMPLES)/good16.bin -hex $(TEMP) -rl4 -ad16 $(SILENT)
	./$(BIN) -bin $(BIN_SAMPLES)/good32.bin -hex $(TEMP) -rl4 -ad32 $(SILENT)
	
test_hex: build setup
	@echo
	### testing hex files...
	
	@echo
	### missing record mark
	-./$(BIN) -hex $(HEX_SAMPLES)/missingRecordMark_1.hex -bin $(TEMP) $(SILENT)
	-./$(BIN) -hex $(HEX_SAMPLES)/missingRecordMark_2.hex -bin $(TEMP) $(SILENT)
	
	@echo
	### missing record delimiter
	-./$(BIN) -hex $(HEX_SAMPLES)/missingRecordDelimiter_1.hex -bin $(TEMP) $(SILENT)
	-./$(BIN) -hex $(HEX_SAMPLES)/missingRecordDelimiter_2.hex -bin $(TEMP) $(SILENT)
	
	@echo
	### missing EOF
	-./$(BIN) -hex $(HEX_SAMPLES)/missingEof.hex -bin $(TEMP) $(SILENT)
	
	@echo
	### wrong checksum
	-./$(BIN) -hex $(HEX_SAMPLES)/wrongChecksum.hex -bin $(TEMP) $(SILENT)
	
	@echo
	### unknown record
	-./$(BIN) -hex $(HEX_SAMPLES)/unknownRecord.hex -bin $(TEMP) $(SILENT)
	
	@echo
	### unknown record (ignored)
	./$(BIN) -hex $(HEX_SAMPLES)/unknownRecord.hex -bin $(TEMP) -ur $(SILENT)
	
	@echo
	### invalid start linear address record (EIP)
	-./$(BIN) -hex $(HEX_SAMPLES)/invalidStartLinearAddressRecord_1.hex -bin $(TEMP) $(SILENT)
	-./$(BIN) -hex $(HEX_SAMPLES)/invalidStartLinearAddressRecord_2.hex -bin $(TEMP) $(SILENT)	
	
	@echo
	### invalid start segment address record (CS and IP)
	-./$(BIN) -hex $(HEX_SAMPLES)/invalidStartSegmentAddressRecord_1.hex -bin $(TEMP) $(SILENT)
	-./$(BIN) -hex $(HEX_SAMPLES)/invalidStartSegmentAddressRecord_2.hex -bin $(TEMP) $(SILENT)
	
	@echo
	### invalid extended linear address record
	-./$(BIN) -hex $(HEX_SAMPLES)/invalidExtendedLinearAddressRecord_1.hex -bin $(TEMP) $(SILENT)
	-./$(BIN) -hex $(HEX_SAMPLES)/invalidExtendedLinearAddressRecord_2.hex -bin $(TEMP) $(SILENT)
	
	@echo
	### invalid extended segment address record
	-./$(BIN) -hex $(HEX_SAMPLES)/invalidExtendedSegmentAddressRecord_1.hex -bin $(TEMP) $(SILENT)
	-./$(BIN) -hex $(HEX_SAMPLES)/invalidExtendedSegmentAddressRecord_2.hex -bin $(TEMP) $(SILENT)
	
	@echo
	### duplicate record
	-./$(BIN) -hex $(HEX_SAMPLES)/duplicateRecord_1.hex -bin $(TEMP) $(SILENT)
	-./$(BIN) -hex $(HEX_SAMPLES)/duplicateRecord_2.hex -bin $(TEMP) $(SILENT)

	@echo
	### exceeded endmost address
	-./$(BIN) -hex $(HEX_SAMPLES)/exceededEndmostAddress_1.hex -bin $(TEMP) -ad8 $(SILENT)
	-./$(BIN) -hex $(HEX_SAMPLES)/exceededEndmostAddress_2.hex -bin $(TEMP) -ad16 $(SILENT)
	
	@echo
	### memory overlap
	-./$(BIN) -hex $(HEX_SAMPLES)/memoryOverlap_1.hex -bin $(TEMP) -ad8 $(SILENT)
	-./$(BIN) -hex $(HEX_SAMPLES)/memoryOverlap_2.hex -bin $(TEMP) -ad16 $(SILENT)
	-./$(BIN) -hex $(HEX_SAMPLES)/memoryOverlap_3.hex -bin $(TEMP) -ad32 $(SILENT)

	@echo
	### good
	./$(BIN) -hex $(HEX_SAMPLES)/good8.hex -bin $(TEMP) -ad8 $(SILENT)
	./$(BIN) -hex $(HEX_SAMPLES)/good16.hex -bin $(TEMP) -ad16 $(SILENT)
	./$(BIN) -hex $(HEX_SAMPLES)/good32.hex -bin $(TEMP) -ad32 $(SILENT)

test_conversion: build setup
	@echo
	### testing conversions...
	
	@echo
	@mkdir -p temp
	./$(BIN) -hex $(HEX_SAMPLES)/good16.hex -bin temp/bin1 -ad32 $(SILENT)
	./$(BIN) -hex $(HEX_SAMPLES)/good16.hex -bin temp/bin2 -ad16 $(SILENT)
	./$(BIN) -bin temp/bin1 -hex temp/hex1 -rl1 $(SILENT)
	./$(BIN) -hex temp/hex1 -bin temp/bin3 -rl1 $(SILENT)
	cmp temp/bin1 temp/bin2
	cmp temp/bin1 temp/bin3
	@rm -rf temp
