
#include "pch.h"
#include "Matrix33.h"
#include <array>
#include <cmath>
#include <iostream>
#include <sstream>

void Matrix33::mul_mat(float* l, float* r, float* res) {
	for (int i = 0; i < 9; i++) {
		res[i] = 0;
		for (int j = 0; j < 3; j++) {
			int l_i = (i / 3) * 3 + j;
			int r_i = i % 3 + j * 3;
			res[i] += l[l_i] * r[r_i];
		}
	}
}

void Matrix33::mul_vec(float* l, float* r, float* res) {
	for (int i = 0; i < 3; i++) {
		res[i] = 0;
		for (int j = 0; j < 3; j++) {
			int l_i = i * 3 + j;
			int r_i = j;
			res[i] += l[l_i] * r[r_i];
		}
	}
}

void Matrix33::compute_rotation_matrix(int ax, float r, float* res) {
	std::array<float, 9> v;
	if (ax == 0)
		v = { 1,0,0, 0,cos(r),-sin(r), 0,sin(r),cos(r) };
	else if (ax == 1)
		v = { cos(r), 0, sin(r), 0, 1, 0, -sin(r), 0, cos(r) };
	else if (ax == 2)
		v = { cos(r),-sin(r),0, sin(r),cos(r),0, 0,0,1 };
	else
		v = { 1,0,0,0,1,0,0,0,1 };
	std::copy(v.begin(), v.end(), res);
}

std::string Matrix33::fmt(float* m) {
	std::stringstream ss;
	for (int i = 0; i < 9; i++) {
		ss << m[i];
		if ((i + 1) % 3 == 0) {
			ss << "\n";
		}
		else {
			ss << " ";
		}
	}
	return ss.str();
}

void Matrix33::test() {
	float l[] = { 1,2,3,4,5,6,7,8,9 };
	float r[] = { 10,11,12,13,14,15,16,17,18 };
	float res[9];
	mul_mat(l, r, res);
	std::cout << fmt(res);
}

void Matrix33::test2() {
	float A[] = { 8,-9,0, 9,8,0, 0,0,1 };
	float B[] = { 6,0,7, 0,1,0, -7,0,6 };
	float C[] = { 1,0,0, 0,2,-3, 0,3,2 };

	float t[9], res[9];
	mul_mat(B, C, t);
	mul_mat(A, t, res);

	std::cout << fmt(res);
}

void Matrix33::test3() {
	float X[9], Y[9], Z[9], t[9], res[9];
	compute_rotation_matrix(0, 0.1f, X);
	compute_rotation_matrix(1, 0.2f, Y);
	compute_rotation_matrix(2, 0.3f, Z);
	mul_mat(Y, X, t);
	mul_mat(Z, t, res);

	std::cout << fmt(res) << "\n";

	float ya = asin(-res[6]);
	float za = asin(res[3] / cos(ya));
	float xa = asin(res[7] / cos(ya));

	std::cout << xa << " " << ya << " " << za << "\n";
}

