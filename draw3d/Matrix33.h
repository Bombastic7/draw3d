#pragma once

#include <string>

namespace Matrix33 {
	void mul_mat(float* l, float* r, float* res);
	void mul_vec(float* l, float* r, float* res);
	void compute_rotation_matrix(int ax, float r, float* res);
	std::string fmt(float* m);
	void test();
	void test2();
	void test3();
}