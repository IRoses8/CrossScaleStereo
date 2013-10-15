#include "CommFunc.h"
#include "SSCA.h"

SSCA::SSCA(const Mat l, const Mat r, const int m, const int d)
	: lImg( l ), rImg( r ), maxDis( m ), disSc( d )
{
	// all input image must be in CV_64FC3 format
	CV_Assert( lImg.type() == CV_64FC3 && rImg.type() == CV_64FC3 );
	wid = lImg.cols;
	hei = lImg.rows;
	// init disparity map
	lDis = Mat::zeros( hei, wid, CV_8UC1 );

	// init cost volum data
	costVol = new Mat[ maxDis  ];
	for( int mIdx = 0; mIdx < maxDis; mIdx ++ ) {
		costVol[ mIdx ] = Mat::zeros( hei, wid, CV_64FC1 );
	}
}



SSCA::~SSCA(void)
{
	delete [] costVol;
}
// get left disparity
Mat SSCA::getLDis()
{
	return lDis;
}
//
// Save Cost Volume
//
void SSCA::saveCostVol(const string fn)
{
	// save as matlab order
	FILE* fp = fopen( fn.c_str(), "w" );
	for( int d = 1; d < maxDis; d ++ ) {
		printf( "-s-v-" );
		for( int x = 0; x < wid; x ++ ) {
			for( int y = 0; y < hei; y ++ ) {
				double* cost = costVol[ d ].ptr<double>( y );
				fprintf( fp, "%lf\n", cost[ x ] );
			}
		}
	}
	fclose( fp );
}
//
// Add previous cost volume from pyramid
//
void SSCA::AddPyrCostVol(SSCA *pre, const double COST_ALPHA )
{
	printf( "\n\tAdd Pyramid Cost: COST_ALPHA = %.2lf", COST_ALPHA );
	for( int d = 1; d < maxDis; d ++ ) {
		int pD = ( d + 1 ) / 2;
		printf( ".a.p." );
		for( int y = 0; y < hei; y ++ ) {
			int pY = y / 2;
			double* cost = costVol[ d ].ptr<double>( y );
			double* pCost = pre->costVol[ pD ].ptr<double>( pY );
			for( int x = 0; x < wid; x ++ ) {
				int pX = x / 2;
				cost[ x ] = COST_ALPHA * cost[ x ] +
					( 1 - COST_ALPHA ) * pCost[ pX ];

			}
		}
	}
}
//
// 1. Cost Computation
//
void SSCA::CostCompute( CCMethod* ccMtd )
{
	printf( "\n\tCost Computation:" );
	if( ccMtd ) {
		ccMtd->buildCV( lImg, rImg, maxDis, costVol );
	} else {
		printf( "\n\t\tDo nothing" );
	}

}
//
// 2. Cost Aggregation
//
void SSCA::CostAggre( CAMethod* caMtd )
{
	printf( "\n\tCost Aggregation:" );
	if( caMtd ) {
		caMtd->aggreCV( lImg, rImg, maxDis, costVol );
	} else {
		printf( "\n\t\tDo nothing" );
	}
	
}
//
// 3. Match
//
void SSCA::Match( void )
{
	printf( "\n\tMatch" );
	for( int y = 0; y < hei; y ++ ) {
		uchar* lDisData = ( uchar* ) lDis.ptr<uchar>( y );
		for( int x = 0; x < wid; x ++ ) {
			double minCost = DOUBLE_MAX;
			int    minDis  = 0;
			for( int d = 1; d < maxDis; d ++ ) {
				double* costData = ( double* )costVol[ d ].ptr<double>( y );
				if( costData[ x ] < minCost ) {
					minCost = costData[ x ];
					minDis  = d;
				}
			}
			lDisData[ x ] = minDis * disSc;
		}
	}
}
//
// 4. Post Process;
//
void SSCA::PostProcess( PPMethod* ppMtd )
{
	printf( "\n\tPostProcess:" );
	if( ppMtd ) {

	} else {
		printf( "\n\t\tDo nothing" );
	}
}
#ifdef _DEBUG
// global function to solve all cost volume
void SolveAll( SSCA**& smPyr, const int PY_LVL, const double REG_LAMBDA )
{
	printf( "\n\t\tSolve All" );
	printf( "\n\t\tReg param: %.4lf\n", REG_LAMBDA );
	// construct regularization matrix
	Mat regMat = Mat::zeros( PY_LVL, PY_LVL, CV_64FC1 );
	for( int s = 0; s < PY_LVL; s ++ ) {
		if( s == 0 ) {
			regMat.at<double>( s, s ) = 1 + REG_LAMBDA;
			regMat.at<double>( s, s + 1 ) = - REG_LAMBDA;
		} else if( s == PY_LVL - 1 ) {
			regMat.at<double>( s, s ) = 1 + REG_LAMBDA;
			regMat.at<double>( s, s - 1 ) = - REG_LAMBDA;
		} else {
			regMat.at<double>( s, s ) = 1 + 2 * REG_LAMBDA;
			regMat.at<double>( s, s - 1 ) = - REG_LAMBDA;
			regMat.at<double>( s, s + 1 ) = - REG_LAMBDA;
		}
	}
	Mat regInv = regMat.inv( );
	double* invWgt  = new double[ PY_LVL * PY_LVL ];
	for( int m = 0; m < PY_LVL; m ++ ) {
		double* sWgt = invWgt + m * PY_LVL;
		for( int s = 0; s < PY_LVL; s ++ ) {
			sWgt[ s ] = regInv.at<double>( m, s );
		}
	}

	PrintMat<double>( regInv );

	int hei = smPyr[ 0 ]->hei;
	int wid = smPyr[ 0 ]->wid;

	// backup all cost
	Mat** newCosts = new Mat*[ PY_LVL ];
	for( int s = 0; s < PY_LVL; s ++ ) {
		newCosts[ s ] = new Mat[ smPyr[ s ]->maxDis ];
		for( int d = 0; d < smPyr[ s ]->maxDis; d ++ ) {
			newCosts[ s ][ d ] = Mat::zeros( smPyr[ s ]->hei, smPyr[ s ]->wid, CV_64FC1  );
		}
	}

	for( int d = 1; d < smPyr[ 0 ]->maxDis; d ++ ) {
		printf( ".s.v." );
		for( int y = 0; y < hei; y ++ ) {
			for( int x = 0; x < wid; x ++ ) {
				for( int m = 0; m < PY_LVL; m ++ ) {
					double sum = 0.0f;
					double* sWgt = invWgt + m * PY_LVL;
					int curY = y;
					int curX = x;
					int curD = d;
					int assY = y;
					int assX = x;
					int assD = d;
					for( int s = 0; s < PY_LVL; s ++ ) {
						if( s == m ) {
							assY = curY;
							assX = curX;
							assD = curD;
						}
						double curCost = smPyr[ s ]->costVol[ curD ].at<double>( curY, curX );
						sum += sWgt[ s ] * curCost;
						curY = curY / 2;
						curX = curX / 2;
						curD = ( curD + 1 ) / 2;
					}
					newCosts[ m ][ assD ].at<double>( assY, assX ) = sum;
				}
			}
		}
	}

	for( int s = 0; s < PY_LVL; s ++ ) {
		for( int d = 0; d < smPyr[ s ]->maxDis; d ++ ) {
			smPyr[ s ]->costVol[ d ] = newCosts[ s ][ d ].clone();
		}
	}
	// PrintMat<double>( smPyr[ 0 ]->costVol[ 1 ] );
	delete [] invWgt;
	for( int s = 0; s < PY_LVL; s ++ ) {
		delete [ ] newCosts[ s ];
	}
	delete [] newCosts;
}

void saveOnePixCost( SSCA**& smPyr, const int PY_LVL )
{
	// save as matlab order
	FILE* fp = fopen( "onePix.txt", "w" );
	for( int y = 0; y < smPyr[ 0 ]->hei; y ++ ) {
		for( int x = 0; x < smPyr[ 0 ]->wid; x ++ ) {
			if( y == 24 && x == 443 ) {
				int curY = y;
				int curX = x;
				int prtTime = 1;
				for( int s = 0; s < PY_LVL; s ++ ) {
					fprintf( fp, "(%d,%d): ", curX, curY );
					for( int d = 1; d < smPyr[ s ]->maxDis; d ++ ) {
						double curCost = smPyr[ s ]->costVol[ d ].at<double>( curY, curX );
						for( int p = 0; p < prtTime; p ++ ) {
							fprintf( fp, " %lf", curCost );
						}
					}
					curY = curY / 2;
					curX = curX / 2;
					prtTime *= 2;
					fprintf( fp, "\n" );
				}
			}
		}
	}
	fclose( fp );
}

#else
// global function to solve all cost volume
void SolveAll( SSCA**& smPyr, const int PY_LVL, const double REG_LAMBDA )
{
	printf( "\n\t\tSolve All" );
	printf( "\n\t\tReg param: %.4lf\n", REG_LAMBDA );
	// construct regularization matrix
	Mat regMat = Mat::zeros( PY_LVL, PY_LVL, CV_64FC1 );
	for( int s = 0; s < PY_LVL; s ++ ) {
		if( s == 0 ) {
			regMat.at<double>( s, s ) = 1 + REG_LAMBDA;
			regMat.at<double>( s, s + 1 ) = - REG_LAMBDA;
		} else if( s == PY_LVL - 1 ) {
			regMat.at<double>( s, s ) = 1 + REG_LAMBDA;
			regMat.at<double>( s, s - 1 ) = - REG_LAMBDA;
		} else {
			regMat.at<double>( s, s ) = 1 + 2 * REG_LAMBDA;
			regMat.at<double>( s, s - 1 ) = - REG_LAMBDA;
			regMat.at<double>( s, s + 1 ) = - REG_LAMBDA;
		}
	}
	Mat regInv = regMat.inv( );
	double* invWgt  = new double[ PY_LVL ];
	for( int s = 0; s < PY_LVL; s ++ ) {
		invWgt[ s ] = regInv.at<double>( 0, s );
	}
	PrintMat<double>( regInv );
	int hei = smPyr[ 0 ]->hei;
	int wid = smPyr[ 0 ]->wid;
	// PrintMat<double>( smPyr[ 0 ]->costVol[ 1 ] );
	// for each cost volume divide its mean value
	//for( int s = 0; s < PY_LVL; s++ ) {
	//	for( int y = 0; y < smPyr[ s ]->hei; y ++ ) {
	//		for( int x = 0; x < smPyr[ s ]->wid; x ++ ) {
	//			double meanD = 0;
	//			for( int d = 1; d < smPyr[ s ]->maxDis; d ++ ) {
	//				meanD += smPyr[ s ]->costVol[ d ].at<double>( y, x );
	//			}
	//			meanD /= ( smPyr[ s ]->maxDis - 1 );
	//			if( meanD == 0 ) {
	//				meanD = 1.0;
	//			}
	//			for( int d = 1; d < smPyr[ s ]->maxDis; d ++ ) {
	//				smPyr[ s ]->costVol[ d ].at<double>( y, x ) /= meanD;
	//			}
	//		}
	//	}
	//}

	for( int d = 1; d < smPyr[ 0 ]->maxDis; d ++ ) {
		// printf( ".s.v." );
		for( int y = 0; y < hei; y ++ ) {
			for( int x = 0; x < wid; x ++ ) {
				int curY = y;
				int curX = x;
				int curD = d;
				double sum = 0;
				for( int s = 0; s < PY_LVL; s ++ ) {
					double curCost = smPyr[ s ]->costVol[ curD ].at<double>( curY, curX );
#ifdef _DEBUG
					if( y == 160 && x == 160 ) {
						printf( "\ns=%d(%d,%d)\td=%d\tcost=%.4lf", s, curY, curX, curD, curCost );
					}
#endif
					sum += invWgt[ s ] * curCost;
					curY = curY / 2;
					curX = curX / 2;
					curD = ( curD + 1 ) / 2;
				}
				smPyr[ 0 ]->costVol[ d ].at<double>( y, x ) = sum;

			}
		}
	}
	// PrintMat<double>( smPyr[ 0 ]->costVol[ 1 ] );
	delete [] invWgt;
}

#endif

