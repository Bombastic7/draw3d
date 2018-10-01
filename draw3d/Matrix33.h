#pragma once

#include <string>

namespace Matrix33 {
	//A few functions for working with 3x3 matrices. All pointers are to arrays of length 9, row-major.
	
	//Multiply two 3x3 matrices l*r. Result placed in res.
	void mul_mat(float* l, float* r, float* res);

	//Multiply a 3x3 matrix l and a 3x1 vector r, l*r. Result placed in res.
	void mul_vec(float* l, float* r, float* res);

	//Compute 3D rotation matrix. ax is 0,1,2 for x,y,z. r is angle in radians (CCW). Result placed in res.
	void compute_rotation_matrix(int ax, float r, float* res);

	//Return pretty string of matrix.
	std::string fmt(float* m);

	void test();
	void test2();
	void test3();
}