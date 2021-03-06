/*
 *  7-state Kalman Filter for gyro and accelerometer processing
 *	Position and gyro bias tracking.
 *
 *  Copyright (c) by Carlos Becker	http://github.com/cbecker 
 *
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 *
 */



#include <openAHRS/kalman/kalman7.h>


#include <Eigen/Core>
#include <Eigen/LU>

USING_PART_OF_NAMESPACE_EIGEN


using namespace std;	//for debugging

namespace openAHRS {

	kalman7::kalman7()
	{
		meas_variance = 0.01;	//just to initialize it
	}

	void	kalman7::KalmanInit( Matrix<FT,3,1> &startAngle, 
						Matrix<FT,3,1> &startBias, FT meas_var,
						FT process_bias_var, FT process_quat_var )
	{
		meas_variance	= meas_var;

		I.setIdentity();
		P.setIdentity();

		A.setIdentity();

		R.setIdentity();
		R	*= meas_variance;

		/** noise model covariance matrix  **/
		W.setIdentity();
		
		W		*= process_quat_var;
		W(4,4)	 = process_bias_var;
		W(5,5)	 = process_bias_var;
		W(6,6)	 = process_bias_var;


		H.setZero();

		/** initial estimate and bias **/
		X.block<4,1>(0,0)	= util::eulerToQuat( startAngle );
		X.block<3,1>(4,0)	= startBias;
	}

	void	kalman7::predictState( Matrix<FT,7,1> &X, 
					const Matrix<FT,3,1> &gyros, FT dt )
	{
		Matrix<FT,4,1>	quat = X.block<4,1>(0,0);
		FT	p = gyros(0) - X(4);
		FT	q = gyros(1) - X(5);
		FT	r = gyros(2) - X(6);

		/* New quaternion estimate */
		X.block<4,1>(0,0) = quat + util::calcQOmega( p, q, r )*quat*dt/2;
	
		/* bias estimates are untouched */
	}


	/** 
	* Calculate jacobian dF(..)/dxi
	*
	* @param A			Destination matrix
	* @param gyros		Gyro data, including bias
	* @param q			Current state in quaternion format
	* @param dt			Delta between updates
	*/
	void	kalman7::calcA( Matrix<FT,7,7> &A, 
					const Matrix<FT,3,1> &gyros,
					const Matrix<FT,4,1> &q, FT dt )
	{
		A.setIdentity();
		if ( true )	//track bias
		{
			A.block<4,4>(0,0)	= Matrix<FT,4,4>::Identity() +
					dt * util::calcQOmega( gyros[0] - X(4), gyros[1] - X(5), gyros[2] - X(6) )/2;

			A.block<1,3>(0,4)	<<	 dt*q[1]/2,  dt*q[2]/2,  dt*q[3]/2;
			A.block<1,3>(1,4)	<<	-dt*q[0]/2,  dt*q[3]/2, -dt*q[2]/2;
			A.block<1,3>(2,4)	<<	-dt*q[3]/2, -dt*q[0]/2,  dt*q[1]/2;
			A.block<1,3>(3,4)	<<	 dt*q[2]/2, -dt*q[1]/2, -dt*q[0]/2;
		}
		else {
			A.block<4,4>(0,0)	= Matrix<FT,4,4>::Identity() +
				dt * util::calcQOmega( gyros(0) - X(4) , gyros(1) - X(5) , gyros(2) - X(6)) /2;
		}
	}

	void	kalman7::KalmanUpdate( int iter, const Matrix<FT,3,1> &angles, FT dt )
	{
		/*if ( iter == 0 )
		{
			cout << "A\n" << A << endl;
			cout << "X\n" << X << endl;
			cout << "R\n" << R << endl;
			cout << "W\n" << W << endl;
			cout << "K\n" << K << endl;
			cout << "P\n" << P << endl;

		}*/

		/** Renormalize quaternion **/
		q	= X.start<4>();
		q.normalize();
		X.start<4>()	= q;

		/*** KALMAN UPDATE **/

		/*- R should be weighted depending on angle,
		 * since real inputs are accels */

		H.block<3,4>(0,0)	= util::calcQMeas( q );


		Matrix<FT,7,3>	Ht	= H.transpose();
		Matrix<FT,3,3> inv;
		( H*P*Ht + R ).computeInverse( &inv );

		if ( isnan( inv(0,0) ) )
			cout << "NAN" << endl;
		
		K	= P*Ht * inv;	
		/*if ( iter == 0 ) {
			cout << "P\n" << P << endl;
			cout << "R\n" << R << endl;
			cout << "K\n" << K << endl;
			cout << "H\n" << H <<endl;
			cout << "Ht\n" << Ht <<endl;
		}*/

		/** predicted quaternion to euler for error calculation **/
		Matrix<FT,3,1>	predAngles	= util::quatToEuler( q );
		angleErr(0)	= util::calcAngleError( angles(0), predAngles(0) );
		angleErr(1)	= util::calcAngleError( angles(1), predAngles(1) );
		angleErr(2)	= util::calcAngleError( angles(2), predAngles(2) );

		X	= X + K*angleErr;

/*		if ( iter == 0 )
			cout << "X\n" << X << endl;*/


	
		/** Renormalize Quaternion **/
		q	= X.start<4>();
		q.normalize();
		X.start<4>()	= q;

		#if 0
			P	= ( I - K*H ) * P;	/* Using this might not be right if 
									calculation problems arise */
		#else
			P	= ( I - K*H ) * P * (( I - K*H ).transpose()) + K*R*(K.transpose());
		#endif
		
	}


	void	kalman7::KalmanPredict( int iter, const Matrix<FT,3,1> &gyros, FT dt )
	{
		/** Predict **/
		calcA( A, gyros, q, dt );

		/** only update quaternion-relevant data in our state vector **/
		predictState( X, gyros, dt );
		
		P	= A*P*(A.transpose()) + W;



/*		if ( iter == 0 )
		{
			cout << "A\n" << A << endl;
			cout << "X\n" << X << endl;
			cout << "P\n" << P << endl;
		}*/

	}


};

