/*
  Challenger, a UCI chinese chess playing engine based on Challenger
  
  Copyright (C) 2013-2014 grefen

  Challenger is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  Challenger is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <algorithm>
#include <cstring>
#include <iostream>

#include "bitboard.h"
#include "bitcount.h"
//#include "misc.h"
#include "rkiss.h"

CACHE_LINE_ALIGNMENT

Bitboard RMasks[SQUARE_NB];
//Bitboard RookR0[SQUARE_NB][512];
//Bitboard RookRL90[SQUARE_NB][1024];
Bitboard RookR0[SQUARE_NB][128];
Bitboard RookRL90[SQUARE_NB][256];


//Bitboard RMagics[SQUARE_NB];
//Bitboard* RAttacks[SQUARE_NB];
//unsigned RShifts[SQUARE_NB];

//Bitboard BMasks[SQUARE_NB];
//Bitboard BMagics[SQUARE_NB];
//Bitboard* BAttacks[SQUARE_NB];
//unsigned BShifts[SQUARE_NB];

//Bitboard CannonSupperR0[SQUARE_NB][512];
//Bitboard CannonSupperRL90[SQUARE_NB][1024];
//Bitboard CannonControlR0[SQUARE_NB][512];
//Bitboard CannonControlRL90[SQUARE_NB][1024];

Bitboard CannonSupperR0[SQUARE_NB][128];
Bitboard CannonSupperRL90[SQUARE_NB][256];
Bitboard CannonControlR0[SQUARE_NB][128];
Bitboard CannonControlRL90[SQUARE_NB][256];

Bitboard KnightStepLeg[SQUARE_NB][4];
Bitboard KnightStepTo[SQUARE_NB][4];
int8_t   KnightStepIndex[SQUARE_NB][SQUARE_NB];

Bitboard BishopStepLeg[SQUARE_NB][4];
Bitboard BishopSetpTo[SQUARE_NB][4];

Bitboard SquareBB[SQUARE_NB];
Bitboard SquareBBL90[SQUARE_NB];
Bitboard FileBB[FILE_NB];
Bitboard RankBB[RANK_NB];
Bitboard AdjacentFilesBB[FILE_NB];
Bitboard InFrontBB[COLOR_NB][RANK_NB];
Bitboard StepAttacksBB[PIECE_NB][SQUARE_NB];//�ȿ�����1-7���͵��ӣ�Ҳ��������1-7 9-15���͵��ӣ���Ϊ�Գ�����,
Bitboard BetweenBB[SQUARE_NB][SQUARE_NB];
Bitboard DistanceRingsBB[SQUARE_NB][10];
Bitboard ForwardBB[COLOR_NB][SQUARE_NB];
Bitboard PassedPawnMask[COLOR_NB][SQUARE_NB];
Bitboard PawnAttackSpan[COLOR_NB][SQUARE_NB];
Bitboard PseudoAttacks[PIECE_TYPE_NB][SQUARE_NB];
Bitboard PawnNoMaskStepAttacksBB[COLOR_NB][SQUARE_NB];

Bitboard CityBB[COLOR_NB];
Bitboard AdvisorCityBB[COLOR_NB];
Bitboard BishopCityBB[COLOR_NB];

Bitboard PawnMask[COLOR_NB];

Bitboard PassedRiverBB[COLOR_NB];

int SquareDistance[SQUARE_NB][SQUARE_NB];

namespace {

  // De Bruijn sequences. See chessprogramming.wikispaces.com/BitScan
  const uint64_t DeBruijn_64 = 0x3F79D71B4CB0A89ULL;
  const uint32_t DeBruijn_32 = 0x783A9B23;

  CACHE_LINE_ALIGNMENT

  int MS1BTable[256];
  Square BSFTable[96];
  int BSFTable64[64];
  Bitboard RTable[0x19000]; // Storage space for rook attacks
  Bitboard BTable[0x1480];  // Storage space for bishop attacks

  typedef unsigned (Fn)(Square, Bitboard);

  void init_magics(Bitboard table[], Bitboard* attacks[], Bitboard magics[],
                   Bitboard masks[], unsigned shifts[], Square deltas[], Fn index);

  Bitboard sliding_attack(Square deltas[], Square sq, Bitboard occupied);

  FORCE_INLINE unsigned bsf_index(Bitboard b) {

    // Matt Taylor's folding for 32 bit systems, extended to 64 bits by Kim Walisch
    //b ^= (b.operator-(1));
    //return Is64Bit ? (b * DeBruijn_64) >> 58
    //               : ((unsigned(b) ^ unsigned(b >> 32)) * DeBruijn_32) >> 26;

	  if(b.low > 0)
	  {
         return (((b.low^(b.low-1))* DeBruijn_32)>>27);
	  }
	  else if(b.mid > 0)
	  {
		  return (((b.mid^(b.mid-1))* DeBruijn_32)>>27) + 32;
	  }
	  else if(b.hight > 0)
	  {
         return (((b.hight^(b.hight-1))* DeBruijn_32)>>27) + 64;
	  }

	  return 0;
  }

  FORCE_INLINE unsigned bsf_index64(uint64_t b) {

	  // Matt Taylor's folding for 32 bit systems, extended to 64 bits by Kim Walisch
	  b ^= (b - 1);
	  return Is64Bit ? (b * DeBruijn_64) >> 58
		  : ((unsigned(b) ^ unsigned(b >> 32)) * DeBruijn_32) >> 26;
  }

  Square square_rotate_left_90(Square sq)
  {
     return Square(file_of(sq) * 10 + (9-rank_of(sq)));;//File(9-rank_of(sq))|Rank(file_of(sq));bug,9-rank_of(sq) will extended
  }

}

/// lsb()/msb() finds the least/most significant bit in a nonzero bitboard.
/// pop_lsb() finds and clears the least significant bit in a nonzero bitboard.

#ifndef USE_BSFQ

Square lsb(Bitboard b) { return BSFTable[bsf_index(b)]; }

Square pop_lsb(Bitboard* b) {

  Bitboard bb = *b;
  *b = bb & (bb.operator-(1));
  return BSFTable[bsf_index(bb)];
}

size_t pop_lsb(uint64_t* b)
{
	uint64_t bb = *b;
	*b = bb & (bb - 1);
	return BSFTable64[bsf_index64(bb)];
}

Square msb(Bitboard b) {

  unsigned b32;
  int result = 0;

  //if (b > 0xFFFFFFFF)
  //{
  //    b >>= 32;
  //    result = 32;
  //}

  //b32 = unsigned(b);

  //if (b32 > 0xFFFF)
  //{
  //    b32 >>= 16;
  //    result += 16;
  //}

  //if (b32 > 0xFF)
  //{
  //    b32 >>= 8;
  //    result += 8;
  //}
  if(b.hight > 0)
  {
      result = 64;

	  b32 = unsigned(b.hight);

	  if (b32 > 0xFFFF)
	  {
		  b32 >>= 16;
		  result += 16;
	  }

	  if (b32 > 0xFF)
	  {
		  b32 >>= 8;
		  result += 8;
	  }
  }
  else if(b.hight == 0 && b.mid > 0)
  {
	  result = 32;

	  b32 = unsigned(b.mid);

	  if (b32 > 0xFFFF)
	  {
		  b32 >>= 16;
		  result += 16;
	  }

	  if (b32 > 0xFF)
	  {
		  b32 >>= 8;
		  result += 8;
	  }
  }
  else 
  {
	  b32 = unsigned(b.low);

	  if (b32 > 0xFFFF)
	  {
		  b32 >>= 16;
		  result += 16;
	  }

	  if (b32 > 0xFF)
	  {
		  b32 >>= 8;
		  result += 8;
	  }
  }  

  return (Square)(result + MS1BTable[b32]);
}

size_t msb(size_t b)
{
	unsigned b32;
	int result = 0;

	b32 = unsigned(b);

	if (b32 > 0xFFFF)
	{
		b32 >>= 16;
		result += 16;
	}

	if (b32 > 0xFF)
	{
		b32 >>= 8;
		result += 8;
	}

	return (Square)(result + MS1BTable[b32]);
}

#endif // ifndef USE_BSFQ


/// Bitboards::print() prints a bitboard in an easily readable format to the
/// standard output. This is sometimes useful for debugging.

void Bitboards::print(Bitboard b) {

  //sync_cout;

  //for (Rank rank = RANK_9; rank >= RANK_0; --rank)
  //{
  //    std::cout << "+---+---+---+---+---+---+---+---+" << '\n';

  //    for (File file = FILE_A; file <= FILE_I; ++file)
  //        std::cout << "| " << (b & (file | rank) ? "X " : "  ");

  //    std::cout << "|\n";
  //}
  //std::cout << "+---+---+---+---+---+---+---+---+" << std::endl;;//sync_endl;

	for (Rank rank = RANK_9; rank >= RANK_0; --rank)
	{
		for (File file = FILE_A; file <= FILE_I; ++file)
		{
			if(file == FILE_A)
			{
				std::cout << rank << " ";
			}


			if((b & (file | rank)))
			{
				std::cout << "X";
			}
			else
			{
				std::cout << "+";
			}

			if(file == FILE_I)
			{

			}
			else
			{
				std::cout << "--";
			}
		}

		std::cout << "\n";
		if(rank != RANK_0)
		   std::cout << "  |  |  |  |  |  |  |  |  |" << '\n';
		else
		   std::cout << "  A  B  C  D  E  F  G  H  I" << '\n'; 
	}
	std::cout << "\n";
	
}


/// Bitboards::init() initializes various bitboard arrays. It is called during
/// program initialization.

void Bitboards::init() {

  for (int k = 0, i = 0; i < 8; ++i)
      while (k < (2 << i))
          MS1BTable[k++] = i;

  for (int i = 0; i < 90; ++i)
  {
	  BSFTable[bsf_index(Bitboard(1,0,0) << i)] = Square(i);	 
  }

  for (int i = 0; i < 64; ++i)
  {
	  BSFTable64[bsf_index64(1ULL << i)] = i;
  }

  for (Square s = SQ_A0; s <= SQ_I9; ++s)
  {
	  SquareBB[s] = (Bitboard(1,0,0) << s);
  }

  for(Square s = SQ_A0; s <= SQ_I9; ++s)
  {
	  SquareBBL90[s] = SquareBB[square_rotate_left_90(s)];
  }

  //FileBB[FILE_A] = FileABB;
  //RankBB[RANK_1] = Rank1BB;

  //for (int i = 1; i < 8; ++i)
  //{
  //    FileBB[i] = FileBB[i - 1] << 1;
  //    RankBB[i] = RankBB[i - 1] << 8;
  //}

  FileBB[FILE_A] = FileABB;
  FileBB[FILE_B] = FileBBB;
  FileBB[FILE_C] = FileCBB;
  FileBB[FILE_D] = FileDBB;
  FileBB[FILE_E] = FileEBB;
  FileBB[FILE_F] = FileFBB;
  FileBB[FILE_G] = FileGBB;
  FileBB[FILE_H] = FileHBB;
  FileBB[FILE_I] = FileIBB;
  
  RankBB[RANK_0] = Rank0BB;
  RankBB[RANK_1] = Rank1BB;
  RankBB[RANK_2] = Rank2BB;
  RankBB[RANK_3] = Rank3BB;
  RankBB[RANK_4] = Rank4BB;
  RankBB[RANK_5] = Rank5BB;
  RankBB[RANK_6] = Rank6BB;
  RankBB[RANK_7] = Rank7BB;
  RankBB[RANK_8] = Rank8BB;
  RankBB[RANK_9] = Rank9BB;

  CityBB[WHITE] = WhiteCityBB;
  CityBB[BLACK] = BlackCityBB;

  AdvisorCityBB[WHITE] = WhiteAdvisorCityBB;
  AdvisorCityBB[BLACK] = BlackAdvisorCityBB;

  BishopCityBB[WHITE] = WhiteBishopCityBB;
  BishopCityBB[BLACK] = BlackBishopCityBB;

  PawnMask[WHITE] = WhitePawnMaskBB;
  PawnMask[BLACK] = BlackPawnMaskBB;

  PassedRiverBB[WHITE] = DarkSquares;
  PassedRiverBB[BLACK] = ~DarkSquares;


  for (File f = FILE_A; f <= FILE_I; ++f)
      AdjacentFilesBB[f] = (f > FILE_A ? FileBB[f - 1] : Bitboard(0,0,0)) | (f < FILE_I ? FileBB[f + 1] : Bitboard(0,0,0));

  for (Rank r = RANK_0; r < RANK_9; ++r)
      InFrontBB[WHITE][r] = ~(InFrontBB[BLACK][r + 1] = InFrontBB[BLACK][r] | RankBB[r]);

  for (Color c = WHITE; c <= BLACK; ++c)
      for (Square s = SQ_A0; s <= SQ_I9; ++s)
      {
          ForwardBB[c][s]      = InFrontBB[c][rank_of(s)] & FileBB[file_of(s)];
          //PawnAttackSpan[c][s] = InFrontBB[c][rank_of(s)] & AdjacentFilesBB[file_of(s)];
          //PassedPawnMask[c][s] = ForwardBB[c][s] | PawnAttackSpan[c][s];

          if(PassedRiverBB[c]&s)
             PawnAttackSpan[c][s] = (InFrontBB[c][rank_of(s)]| RankBB[rank_of(s)]) & PawnMask[c];
		  else
		     PawnAttackSpan[c][s] = ForwardBB[c][s] | PassedRiverBB[c];

          PassedPawnMask[c][s] = (InFrontBB[c][rank_of(s)]| RankBB[rank_of(s)]) & PassedRiverBB[c];//?
      }

  for (Square s1 = SQ_A0; s1 <= SQ_I9; ++s1)
      for (Square s2 = SQ_A0; s2 <= SQ_I9; ++s2)
      {
          SquareDistance[s1][s2] = std::max(file_distance(s1, s2), rank_distance(s1, s2));
          if (s1 != s2)
             DistanceRingsBB[s1][SquareDistance[s1][s2] - 1] |= s2;
      }

  int steps[][9] = { {}, { 9, -1, 1 }, {16, 20, -16, -20}, {10, 8, -8, -10}, { 19, 17, 11, 7, -7, -11, -17, -19 },
  {}, {}, { 9, -1, 1, -9} };

  for (Color c = WHITE; c <= BLACK; ++c)
  {   
	  for (PieceType pt = PAWN; pt <= KING; ++pt)
      for (Square s = SQ_A0; s <= SQ_I9; ++s)
	  for (int k = 0; steps[pt][k]; k++)
	  {
		  Square to = s + Square(c == WHITE ? steps[pt][k] : -steps[pt][k]);

		  if (is_ok(to) && square_distance(s, to) < 3)
		  {	
			  if(pt == KING && square_in_city(c, to) && square_in_city(c, s))
			  {
				  StepAttacksBB[make_piece(c, pt)][s] |= to;
			  }
			  else if(pt == ADVISOR && advisor_in_city(c, to) && advisor_in_city(c, s))
			  {
				  StepAttacksBB[make_piece(c, pt)][s] |= to;
			  }
			  else if(pt == BISHOP && bishop_in_city(c, to)  && bishop_in_city(c, s))
			  {
				  StepAttacksBB[make_piece(c, pt)][s] |= to;
			  }
			  else if(pt == PAWN && pawn_square_ok(c, to) && pawn_square_ok(c, s))
			  {
				  StepAttacksBB[make_piece(c, pt)][s] |= to;
			  }
			  else if(pt == KNIGHT)
			  {
				  StepAttacksBB[make_piece(c, pt)][s] |= to;
			  }

			  if(pt == PAWN)
			  {
				  PawnNoMaskStepAttacksBB[c][s] |= to;
			  }
		  }
	  }
  }


  int knightlegs[5] = {9, 1, -9, -1, 0};
  int knightsteps[5][3]= {{17,19},{11, -7},{-17, -19},{-11, 7}};
  int bishoplegs[5] = {8, 10, -8, -10, 0};
  int bishopsetps[5]= {16, 20, -16, -20};

  for (Square s = SQ_A0; s <= SQ_I9; ++s)
  {				  
	  for (int k = 0; knightlegs[k]; k++)
	  {
		  int to = int(s) + (knightlegs[k]);

		  if (is_ok(to) && square_distance(Square(s), Square(to)) == 1)
		  {
			   KnightStepLeg[s][k] = SquareBB[to];			  
		  }

		  to = int(s) + (knightsteps[k][0]);
		  if (is_ok(to) && square_distance(Square(s), Square(to)) < 3)
		  {
			  KnightStepTo[s][k] |= Square(to); 
			  KnightStepIndex[s][to] = k;
		  }

		  to = int(s) + (knightsteps[k][1]);
		  if (is_ok(to) && square_distance(Square(s), Square(to)) < 3)
		  {
			  KnightStepTo[s][k] |= Square(to);
			  KnightStepIndex[s][to] = k;
		  }
	  }

	  for (int k = 0; bishoplegs[k]; k++)
	  {
		  Square to = s + Square(bishoplegs[k]);
		  if (is_ok(to) && square_distance(Square(s), Square(to)) == 1)
		  {
			  BishopStepLeg[s][k] = SquareBB[to]; 
		  }

		  to = s + Square(bishopsetps[k]);
		  if (is_ok(to) && bishop_in_city(square_color(s), to) && square_distance(Square(s), Square(to)) == 2)
		  {
			  BishopSetpTo[s][k] = SquareBB[to]; 
		  }
	  }
  }


  Square RDeltas[] = { DELTA_N,  DELTA_E,  DELTA_S,  DELTA_W  };
  //Square BDeltas[] = { DELTA_NE, DELTA_SE, DELTA_SW, DELTA_NW };

  //init_magics(RTable, RAttacks, RMagics, RMasks, RShifts, RDeltas, magic_index<ROOK>);
  //init_magics(BTable, BAttacks, BMagics, BMasks, BShifts, BDeltas, magic_index<BISHOP>);

  for(Square s = SQ_A0; s <= SQ_I9; ++s)
  {
       RMasks[s]  = sliding_attack(RDeltas, s, Bitboard()) ;
  }

  Square RDeltasR0[] = { DELTA_E, DELTA_W};
  Square RDeltasR90[] = { DELTA_N, DELTA_S};
  for(Square s = SQ_A0; s <= SQ_I9; ++s)
  {
	  
	  //// rook rank
	  //for(int i = 0; i < 512; ++i)
	  //{         
		 // int f;
		 // int p;
		 // for(f = file_of(s)-1, p = s - 1; f >= 0; p--, f--) {
			//  RookR0[s][i] |= SquareBB[p];
			//  if((i)&(1<<f))	 break;
		 // } for(f = file_of(s) + 1, p = s + 1; f < 9; p++, f++) {
			//  RookR0[s][i] |= SquareBB[p];
			//  if((i)&(1<<f))	 break;
		 // }
	  //}
   //   // rook file
	  //for(int i = 0; i < 1024; ++i)
	  //{
		 // int r;
		 // int p;
		 // for(r = rank_of(s) - 1, p = s - 9; r >= 0; p -= 9, r--) {
			//  RookRL90[s][i] |= SquareBB[p];
			//  if((i)&(1<<(9-r)))break;
		 // } for(r = rank_of(s) + 1, p = s + 9; r < 10; p += 9, r++) {
			//  RookRL90[s][i] |= SquareBB[p];
			//  if((i)&(1<<(9-r)))break;
		 // }
	  //}
	  // rook rank
	  // �������޹ؽ�Ҫ�ģ���������û���ӣ�����Ϊ���ԳԵ�
	  for(int i = 0; i < 128; ++i)
	  {         
		  int f;
		  int p;
		  for(f = file_of(s)-1, p = s - 1; f >= 0; p--, f--) {
			  RookR0[s][i] |= SquareBB[p];
			  if((i<<1)&(1<<f))	 break;
		  } for(f = file_of(s) + 1, p = s + 1; f < 9; p++, f++) {
			  RookR0[s][i] |= SquareBB[p];
			  if((i<<1)&(1<<f))	 break;
		  }
	  }
      // rook file
	  for(int i = 0; i < 256; ++i)
	  {
		  int r;
		  int p;
		  for(r = rank_of(s) - 1, p = s - 9; r >= 0; p -= 9, r--) {
			  RookRL90[s][i] |= SquareBB[p];
			  if((i<<1)&(1<<(9-r)))break;
		  } for(r = rank_of(s) + 1, p = s + 9; r < 10; p += 9, r++) {
			  RookRL90[s][i] |= SquareBB[p];
			  if((i<<1)&(1<<(9-r)))break;
		  }
	  }

	  //// cannon rank
	  //for(int i = 0; i < 512; ++i)
	  //{         
		 // int f;
		 // int p;
		 // for(f = file_of(s)-1, p = s - 1; f >= 0; p--, f--) {			 
			//  if((i)&(1<<f))	 break;// battery
		 // } 
		 // for(p--, f--; f >= 0; p--, f--) {
			//  CannonControlR0[s][i] |= SquareBB[p];
			//  if((i)&(1<<f))	 break;
		 // }
		 // for(p--, f--; f >= 0; p--, f--) {	
			//  CannonSupperR0[s][i] |= SquareBB[p];
			//  if((i)&(1<<f))
			//  {
			//	  //CannonSupperR0[s][i] = SquareBB[p];
			//	  break;
			//  }
		 // }

		 // 
		 // for(f = file_of(s) + 1, p = s + 1; f < 9; p++, f++) {			 
			//  if((i)&(1<<f))	 break;// battery
		 // }
		 // for(p++, f++; f < 9; p++, f++) {
			//  CannonControlR0[s][i] |= SquareBB[p];
			//  if((i)&(1<<f))	 break;
		 // }
		 // for(p++, f++; f < 9; p++, f++) {			  
			//  CannonSupperR0[s][i] |= SquareBB[p];
			//  if((i)&(1<<f))
			//  {
			//	  //CannonSupperR0[s][i] = SquareBB[p];
			//	  break;
			//  }
		 // }
	  //}

	  //// cannon file
	  //for(int i = 0; i < 1024; ++i)
	  //{
		 // int r;
		 // int p;
		 // for(r = rank_of(s) - 1, p = s - 9; r >= 0; p -= 9, r--) {
			//  if((i)&(1<<(9-r)))break;// battery
		 // }
		 // for(p -= 9, r--; r >= 0; p -= 9, r--) {
			//  CannonControlRL90[s][i] |= SquareBB[p];
			//  if((i)&(1<<(9-r)))break;
		 // }
		 // for(p -= 9, r--; r >= 0; p -= 9, r--) {
			//  CannonSupperRL90[s][i] |= SquareBB[p];
			//  if((i)&(1<<(9-r)))
			//  {
			//	  //CannonSupperRL90[s][i] |= SquareBB[p];
			//	  break;
			//  }
		 // }

		 // for(r = rank_of(s) + 1, p = s + 9; r < 10; p += 9, r++) {
			//  if((i)&(1<<(9-r)))break;// battery
		 // }
		 // for(p += 9, r++; r < 10; p += 9, r++) {
			//  CannonControlRL90[s][i] |= SquareBB[p];
			//  if((i)&(1<<(9-r)))break;
		 // }
		 // for(p += 9, r++; r < 10; p += 9, r++) {	
			//  CannonSupperRL90[s][i] |= SquareBB[p];
			//  if((i)&(1<<(9-r)))
			//  {
			//	  //CannonSupperRL90[s][i] |= SquareBB[p];
			//	  break;
			//  }
		 // }
	  //}

	   	  // cannon rank
	  // �������޹ؽ�Ҫ�ģ���������û���ӣ�����Ϊ���ԳԵ�����rook��ͬ
	  for(int i = 0; i < 128; ++i)
	  {         
		  int f;
		  int p;
		  for(f = file_of(s)-1, p = s - 1; f >= 0; p--, f--) {			 
			  if((i<<1)&(1<<f))	 break;// battery
		  } 
		  for(p--, f--; f >= 0; p--, f--) {
			  CannonControlR0[s][i] |= SquareBB[p];
			  if((i<<1)&(1<<f))	 break;
		  }
		  for(p--, f--; f >= 0; p--, f--) {	
			  CannonSupperR0[s][i] |= SquareBB[p];
			  if((i<<1)&(1<<f))
			  {
				  //CannonSupperR0[s][i] = SquareBB[p];
				  break;
			  }
		  }

		  
		  for(f = file_of(s) + 1, p = s + 1; f < 9; p++, f++) {			 
			  if((i<<1)&(1<<f))	 break;// battery
		  }
		  for(p++, f++; f < 9; p++, f++) {
			  CannonControlR0[s][i] |= SquareBB[p];
			  if((i<<1)&(1<<f))	 break;
		  }
		  for(p++, f++; f < 9; p++, f++) {			  
			  CannonSupperR0[s][i] |= SquareBB[p];
			  if((i<<1)&(1<<f))
			  {
				  //CannonSupperR0[s][i] = SquareBB[p];
				  break;
			  }
		  }
	  }

	  // cannon file
	  for(int i = 0; i < 256; ++i)
	  {
		  int r;
		  int p;
		  for(r = rank_of(s) - 1, p = s - 9; r >= 0; p -= 9, r--) {
			  if((i<<1)&(1<<(9-r)))break;// battery
		  }
		  for(p -= 9, r--; r >= 0; p -= 9, r--) {
			  CannonControlRL90[s][i] |= SquareBB[p];
			  if((i<<1)&(1<<(9-r)))break;
		  }
		  for(p -= 9, r--; r >= 0; p -= 9, r--) {
			  CannonSupperRL90[s][i] |= SquareBB[p];
			  if((i<<1)&(1<<(9-r)))
			  {
				  //CannonSupperRL90[s][i] |= SquareBB[p];
				  break;
			  }
		  }

		  for(r = rank_of(s) + 1, p = s + 9; r < 10; p += 9, r++) {
			  if((i<<1)&(1<<(9-r)))break;// battery
		  }
		  for(p += 9, r++; r < 10; p += 9, r++) {
			  CannonControlRL90[s][i] |= SquareBB[p];
			  if((i<<1)&(1<<(9-r)))break;
		  }
		  for(p += 9, r++; r < 10; p += 9, r++) {	
			  CannonSupperRL90[s][i] |= SquareBB[p];
			  if((i<<1)&(1<<(9-r)))
			  {
				  //CannonSupperRL90[s][i] |= SquareBB[p];
				  break;
			  }
		  }
	  }
  }


  for (Square s = SQ_A0; s <= SQ_I9; ++s)
  {
      //PseudoAttacks[QUEEN][s]  = PseudoAttacks[BISHOP][s] = attacks_bb<BISHOP>(s, 0);
      //PseudoAttacks[QUEEN][s] |= PseudoAttacks[  ROOK][s] = attacks_bb<  ROOK>(s, 0);
	  PseudoAttacks[ROOK][s] = RMasks[s];
  }

  for (Square s1 = SQ_A0; s1 <= SQ_I9; ++s1)
      for (Square s2 = SQ_A0; s2 <= SQ_I9; ++s2)
          if (PseudoAttacks[ROOK][s1] & s2)
          {
              Square delta = (s2 - s1) / square_distance(s1, s2);

              for (Square s = s1 + delta; s != s2; s += delta)
                  BetweenBB[s1][s2] |= s;
          }
}


namespace {

  Bitboard sliding_attack(Square deltas[], Square sq, Bitboard occupied) {

    Bitboard attack(0,0,0);

    for (int i = 0; i < 4; ++i)
        for (Square s = sq + deltas[i];
             is_ok(s) && square_distance(s, s - deltas[i]) == 1;
             s += deltas[i])
        {
            attack |= s;

            if (occupied & s)
                break;
        }

    return attack;
  }


  //Bitboard pick_random(RKISS& rk, int booster) {

  //  // Values s1 and s2 are used to rotate the candidate magic of a
  //  // quantity known to be the optimal to quickly find the magics.
  //  int s1 = booster & 63, s2 = (booster >> 6) & 63;

  //  Bitboard m = rk.rand<Bitboard>();
  //  m = (m >> s1) | (m << (64 - s1));
  //  m &= rk.rand<Bitboard>();
  //  m = (m >> s2) | (m << (64 - s2));
  //  return m & rk.rand<Bitboard>();
  //}


  // init_magics() computes all rook and bishop attacks at startup. Magic
  // bitboards are used to look up attacks of sliding pieces. As a reference see
  // chessprogramming.wikispaces.com/Magic+Bitboards. In particular, here we
  // use the so called "fancy" approach.

  //void init_magics(Bitboard table[], Bitboard* attacks[], Bitboard magics[],
  //                 Bitboard masks[], unsigned shifts[], Square deltas[], Fn index) {

  //  int MagicBoosters[][8] = { { 3191, 2184, 1310, 3618, 2091, 1308, 2452, 3996 },
  //                             { 1059, 3608,  605, 3234, 3326,   38, 2029, 3043 } };
  //  RKISS rk;
  //  Bitboard occupancy[4096], reference[4096], edges, b;
  //  int i, size, booster;

  //  // attacks[s] is a pointer to the beginning of the attacks table for square 's'
  //  attacks[SQ_A1] = table;

  //  for (Square s = SQ_A1; s <= SQ_H8; ++s)
  //  {
  //      // Board edges are not considered in the relevant occupancies
  //      edges = ((Rank1BB | Rank8BB) & ~rank_bb(s)) | ((FileABB | FileHBB) & ~file_bb(s));

  //      // Given a square 's', the mask is the bitboard of sliding attacks from
  //      // 's' computed on an empty board. The index must be big enough to contain
  //      // all the attacks for each possible subset of the mask and so is 2 power
  //      // the number of 1s of the mask. Hence we deduce the size of the shift to
  //      // apply to the 64 or 32 bits word to get the index.
  //      masks[s]  = sliding_attack(deltas, s, 0) & ~edges;
  //      shifts[s] = (Is64Bit ? 64 : 32) - popcount<Max15>(masks[s]);

  //      // Use Carry-Rippler trick to enumerate all subsets of masks[s] and
  //      // store the corresponding sliding attack bitboard in reference[].
  //      b = size = 0;
  //      do {
  //          occupancy[size] = b;
  //          reference[size++] = sliding_attack(deltas, s, b);
  //          b = (b - masks[s]) & masks[s];
  //      } while (b);

  //      // Set the offset for the table of the next square. We have individual
  //      // table sizes for each square with "Fancy Magic Bitboards".
  //      if (s < SQ_H8)
  //          attacks[s + 1] = attacks[s] + size;

  //      booster = MagicBoosters[Is64Bit][rank_of(s)];

  //      // Find a magic for square 's' picking up an (almost) random number
  //      // until we find the one that passes the verification test.
  //      do {
  //          do magics[s] = pick_random(rk, booster);
  //          while (popcount<Max15>((magics[s] * masks[s]) >> 56) < 6);

  //          std::memset(attacks[s], 0, size * sizeof(Bitboard));

  //          // A good magic must map every possible occupancy to an index that
  //          // looks up the correct sliding attack in the attacks[s] database.
  //          // Note that we build up the database for square 's' as a side
  //          // effect of verifying the magic.
  //          for (i = 0; i < size; ++i)
  //          {
  //              Bitboard& attack = attacks[s][index(s, occupancy[i])];

  //              if (attack && attack != reference[i])
  //                  break;

  //              assert(reference[i] != 0);

  //              attack = reference[i];
  //          }
  //      } while (i != size);
  //  }
  //}
}