/**
* Licensed to the Apache Software Foundation (ASF) under one
* or more contributor license agreements.  See the NOTICE file
* distributed with this work for additional information
* regarding copyright ownership.  The ASF licenses this file
* to you under the Apache License, Version 2.0 (the
* "License"); you may not use this file except in compliance
* with the License.  You may obtain a copy of the License at
*
*     http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*/

#ifndef ORC_RLEV2_HH
#define ORC_RLEV2_HH

#include "Adaptor.hh"
#include "orc/Exceptions.hh"
#include "RLE.hh"

#include <vector>

#define MIN_REPEAT 3
#define HIST_LEN 32
namespace orc {

struct FixedBitSizes {
    enum FBS {
        ONE = 0, TWO, THREE, FOUR, FIVE, SIX, SEVEN, EIGHT, NINE, TEN, ELEVEN, TWELVE,
        THIRTEEN, FOURTEEN, FIFTEEN, SIXTEEN, SEVENTEEN, EIGHTEEN, NINETEEN,
        TWENTY, TWENTYONE, TWENTYTWO, TWENTYTHREE, TWENTYFOUR, TWENTYSIX,
        TWENTYEIGHT, THIRTY, THIRTYTWO, FORTY, FORTYEIGHT, FIFTYSIX, SIXTYFOUR, SIZE
    };
};

enum EncodingType { SHORT_REPEAT=0, DIRECT=1, PATCHED_BASE=2, DELTA=3 };

struct EncodingOption {
  EncodingType encoding;
  int64_t fixedDelta;
  int64_t gapVsPatchListCount;
  int64_t zigzagLiteralsCount;
  int64_t baseRedLiteralsCount;
  int64_t adjDeltasCount;
  uint32_t zzBits90p;
  uint32_t zzBits100p;
  uint32_t brBits95p;
  uint32_t brBits100p;
  uint32_t bitsDeltaMax;
  uint32_t patchWidth;
  uint32_t patchGapWidth;
  uint32_t patchLength;
  int64_t min;
  bool isFixedDelta;
};

class RleEncoderV2 : public RleEncoder {
public:
    RleEncoderV2(std::unique_ptr<BufferedOutputStream> outStream, bool hasSigned, bool alignBitPacking = true);

    ~RleEncoderV2() override {
      delete [] literals;
      delete [] gapVsPatchList;
      delete [] zigzagLiterals;
      delete [] baseRedLiterals;
      delete [] adjDeltas;
    }
    /**
     * Flushing underlying BufferedOutputStream
     */
    uint64_t flush() override;

    void write(int64_t val) override;

private:

    const bool alignedBitPacking;
    uint32_t fixedRunLength;
    uint32_t variableRunLength;
    int64_t prevDelta;
    int32_t histgram[HIST_LEN];

    // The four list below should actually belong to EncodingOption since it only holds temporal values in write(int64_t val),
    // it is move here for performance consideration.
    int64_t* gapVsPatchList;
    int64_t*  zigzagLiterals;
    int64_t*  baseRedLiterals;
    int64_t*  adjDeltas;

    uint32_t getOpCode(EncodingType encoding);
    int64_t* prepareForDirectOrPatchedBase(EncodingOption& option);
    void determineEncoding(EncodingOption& option);
    void computeZigZagLiterals(EncodingOption& option);
    void preparePatchedBlob(EncodingOption& option);

    void writeInts(int64_t* input, uint32_t offset, size_t len, uint32_t bitSize);
    void initializeLiterals(int64_t val);
    void writeValues(EncodingOption& option);
    void writeShortRepeatValues(EncodingOption& option);
    void writeDirectValues(EncodingOption& option);
    void writePatchedBasedValues(EncodingOption& option);
    void writeDeltaValues(EncodingOption& option);
    uint32_t percentileBits(int64_t* data, size_t offset, size_t length, double p, bool reuseHist = false);
};

class RleDecoderV2 : public RleDecoder {
public:
  RleDecoderV2(std::unique_ptr<SeekableInputStream> input,
               bool isSigned, MemoryPool& pool);

  /**
  * Seek to a particular spot.
  */
  void seek(PositionProvider&) override;

  /**
  * Seek over a given number of values.
  */
  void skip(uint64_t numValues) override;

  /**
  * Read a number of values into the batch.
  */
  void next(int64_t* data, uint64_t numValues,
            const char* notNull) override;

private:

  // Used by PATCHED_BASE
  void adjustGapAndPatch() {
    curGap = static_cast<uint64_t>(unpackedPatch[patchIdx]) >>
      patchBitSize;
    curPatch = unpackedPatch[patchIdx] & patchMask;
    actualGap = 0;

    // special case: gap is >255 then patch value will be 0.
    // if gap is <=255 then patch value cannot be 0
    while (curGap == 255 && curPatch == 0) {
      actualGap += 255;
      ++patchIdx;
      curGap = static_cast<uint64_t>(unpackedPatch[patchIdx]) >>
        patchBitSize;
      curPatch = unpackedPatch[patchIdx] & patchMask;
    }
    // add the left over gap
    actualGap += curGap;
  }

  void resetReadLongs() {
    bitsLeft = 0;
    curByte = 0;
  }

  void resetRun() {
    resetReadLongs();
    bitSize = 0;
  }

  unsigned char readByte();

  int64_t readLongBE(uint64_t bsz);
  int64_t readVslong();
  uint64_t readVulong();
  uint64_t readLongs(int64_t *data, uint64_t offset, uint64_t len,
                     uint64_t fbs, const char* notNull = nullptr);

  void readLongsWithoutNulls(int64_t *data, uint64_t offset, uint64_t len,
                             uint64_t fbs);
  void unrolledUnpack4(int64_t *data, uint64_t offset, uint64_t len);
  void unrolledUnpack8(int64_t *data, uint64_t offset, uint64_t len);
  void unrolledUnpack16(int64_t *data, uint64_t offset, uint64_t len);
  void unrolledUnpack24(int64_t *data, uint64_t offset, uint64_t len);
  void unrolledUnpack32(int64_t *data, uint64_t offset, uint64_t len);
  void unrolledUnpack40(int64_t *data, uint64_t offset, uint64_t len);
  void unrolledUnpack48(int64_t *data, uint64_t offset, uint64_t len);
  void unrolledUnpack56(int64_t *data, uint64_t offset, uint64_t len);
  void unrolledUnpack64(int64_t *data, uint64_t offset, uint64_t len);

  uint64_t nextShortRepeats(int64_t* data, uint64_t offset, uint64_t numValues,
                            const char* notNull);
  uint64_t nextDirect(int64_t* data, uint64_t offset, uint64_t numValues,
                      const char* notNull);
  uint64_t nextPatched(int64_t* data, uint64_t offset, uint64_t numValues,
                       const char* notNull);
  uint64_t nextDelta(int64_t* data, uint64_t offset, uint64_t numValues,
                     const char* notNull);

  const std::unique_ptr<SeekableInputStream> inputStream;
  const bool isSigned;

  unsigned char firstByte;
  uint64_t runLength;
  uint64_t runRead;
  const char *bufferStart;
  const char *bufferEnd;
  int64_t deltaBase; // Used by DELTA
  uint64_t byteSize; // Used by SHORT_REPEAT and PATCHED_BASE
  int64_t firstValue; // Used by SHORT_REPEAT and DELTA
  int64_t prevValue; // Used by DELTA
  uint32_t bitSize; // Used by DIRECT, PATCHED_BASE and DELTA
  uint32_t bitsLeft; // Used by readLongs when bitSize < 8
  uint32_t curByte; // Used by anything that uses readLongs
  uint32_t patchBitSize; // Used by PATCHED_BASE
  uint64_t unpackedIdx; // Used by PATCHED_BASE
  uint64_t patchIdx; // Used by PATCHED_BASE
  int64_t base; // Used by PATCHED_BASE
  uint64_t curGap; // Used by PATCHED_BASE
  int64_t curPatch; // Used by PATCHED_BASE
  int64_t patchMask; // Used by PATCHED_BASE
  int64_t actualGap; // Used by PATCHED_BASE
  DataBuffer<int64_t> unpacked; // Used by PATCHED_BASE
  DataBuffer<int64_t> unpackedPatch; // Used by PATCHED_BASE
};
}  // namespace orc

#endif  // ORC_RLEV2_HH
