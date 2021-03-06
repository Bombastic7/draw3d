#include "pch.h"
#include <Windows.h>
#include <algorithm>
#include <array>
#include <iostream>
#include <fstream>
#include <limits>
#include <sstream>
#include <vector>
#include <cmath>
#include "json.hpp"
#include "Matrix33.h"

using json = nlohmann::json;

const char* TEST_CUBE = "{\
\"vertices\" : [0.0,0.0,0.0, 1.0,0.0,0.0, 1.0,1.0,0.0, 0.0,1.0,0.0, 0.0,0.0,1.0, 1.0,0.0,1.0, 1.0,1.0,1.0, 0.0,1.0,1.0],\
\"faces\" : [0,1,2, 0,2,3, 0,1,5, 0,5,4, 1,2,6, 1,6,5, 2,3,7, 2,7,6, 3,0,4, 3,4,7, 4,5,6, 4,6,7],\
\"cam_pos\" : [2,2,-2],\
\"cam_angle\" : [0.0, 0.0, 0.0],\
\"surf_pos\" : [0.0, 0.0, 1.0],\
\"surf_res\" : [500, 500],\
\"surf_lims\" : [-1, 1, -1, 1],\
\"color\" : [255,0,0, 255,0,0, 0,255,0, 0,255,0, 0,0,255, 0,0,255, 255,255,0, 255,255,0, 255,0,255, 255,0,255, 0,255,255, 0,255,255]\
}";




//Compute barycentric coordinates of point p = [x,y,z], given triangle defined by points a,b,c.
//Results returned in res = [s,t,u], functions also returns true if p is inside triangle, else false.
static bool compute_barycentric_coords(float* p, float* a, float* b, float* c, float* res) {
	float d = (b[1] - c[1])*(a[0] - c[0]) + (c[0] - b[0])*(a[1] - c[1]);
	res[0] = ((b[1] - c[1])*(p[0] - c[0]) + (c[0] - b[0])*(p[1] - c[1])) / d;
	res[1] = ((c[1] - a[1])*(p[0] - c[0]) + (a[0] - c[0])*(p[1] - c[1])) / d;
	res[2] = 1 - res[0] - res[1];
	return (res[0] >= 0) & (res[0] <= 1) & (res[1] >= 0) & (res[1] <= 1) & (res[2] >= 0) & (res[2] <= 1);
}


//Class for managing scene information and rendering. Initialise with an appropriate configuration and access current framebuffer with get_px()/release_px().
//Alter camera position and angle with incr_cam_pos()/incr_cam_angle().
//Creates a worker thread that automatically updates framebuffer on construction. Thread terminated on destruction.
// CONFIG:
//	"vertices": array of floats. Coords of vertex n at (vertices[n*3], vertices[n*3+1], vertices[n*3+2]) = (x,y,z)
//	"faces": array of ints. Each face is a 3-tuple of vertices.
//	"color": array of ints. Face n has colour (color[n*3], color[n*3+1], color[n*3+2]) = (R,G,B)
//	"cam_pos": 3-array of floats. x,y,z coords of camera position.
//	"cam_angle": 3-array of floats. x,y,z rotations (radians) of camera.
//	"surf_pos": 3-array of floats. x,y,z coords of centre of view surface in camera space.
//	"surf_lims": 4-array of floats. [left,right,bottom,top] limits in camera space of view surface that will be rendered.
//	"surf_res": 2-array of ints. [pixels in x, pixels in y]
//
//The camera is initially positioned at world space origin, pointing towards +z axis with +y as up.
//Framebuffer is x-major, pixel 0 is (-x,+y) corner of view.
class SceneInfo {
	std::vector<float> vertices;
	std::vector<int> faces;
	std::vector<COLORREF> color;
	std::array<float, 3> cam_pos, cam_angle, surf_pos;
	std::array<float, 4> surf_lims;
	std::array<int, 2> surf_res;
	std::vector<COLORREF> px;
	HANDLE px_mutex;
	HANDLE worker_thread;
	bool worker_running;

	std::array<float, 9> cam_matrix, inv_cam_matrix;

	static DWORD WINAPI draw_thread_entry(LPVOID lpParameter) {
		SceneInfo* scene_info = (SceneInfo*)lpParameter;
		scene_info->worker_routine();
		return 0;
	}

	void worker_routine() {
		std::cout << "worker_routine started\n";

		std::vector<COLORREF> px_swap(surf_res[0] * surf_res[1]);
		while (worker_running) {
			render(px_swap);
			WaitForSingleObject(px_mutex, INFINITE);
			std::vector<COLORREF>& t = px;
			px = px_swap;
			px_swap = t;
			ReleaseMutex(px_mutex);
		}
		std::cout << "worker_routine exiting\n";
	}

	void compute_cam_matrix() {
		float x_rot[9];
		float y_rot[9];
		float z_rot[9];
		float t[9];
		Matrix33::compute_rotation_matrix(0, -cam_angle[0], x_rot);
		Matrix33::compute_rotation_matrix(1, -cam_angle[1], y_rot);
		Matrix33::compute_rotation_matrix(2, -cam_angle[2], z_rot);
		Matrix33::mul_mat(y_rot, z_rot, t);
		Matrix33::mul_mat(x_rot, t, &cam_matrix[0]);

		Matrix33::compute_rotation_matrix(0, cam_angle[0], x_rot);
		Matrix33::compute_rotation_matrix(1, cam_angle[1], y_rot);
		Matrix33::compute_rotation_matrix(2, cam_angle[2], z_rot);
		Matrix33::mul_mat(y_rot, x_rot, t);
		Matrix33::mul_mat(z_rot, t, &inv_cam_matrix[0]);
	}

	int render(std::vector<COLORREF>& res) {
		//Initialise framebuffer
		std::fill(res.begin(), res.end(), 0);

		//Compute projected vertices
		std::vector<float> pv(vertices.size());
		for (unsigned i = 0; i < vertices.size() / 3; i++) {
			float c[3] = { vertices[i * 3] - cam_pos[0], vertices[i * 3 + 1] - cam_pos[1], vertices[i * 3 + 2] - cam_pos[2] };
			float d[3];
			Matrix33::mul_vec(&cam_matrix[0], c, d);
			float a = surf_pos[2] / d[2];
			pv[i * 3] = a * d[0] + surf_pos[0];
			pv[i * 3 + 1] = a * d[1] + surf_pos[1];
			pv[i * 3 + 2] = d[2];
		}

		//xr and yr are total view range. xa and ya are half pixel widths. 
		float xr = abs(surf_lims[0]) + abs(surf_lims[1]);
		float yr = abs(surf_lims[2]) + abs(surf_lims[3]);
		float xa = xr / (2 * surf_res[0]);
		float ya = yr / (2 * surf_res[1]);

		//For each pixel (xi,yi)..
		for (int yi = 0; yi < surf_res[1]; yi++) {
			for (int xi = 0; xi < surf_res[0]; xi++) {
				//Compute central pixel position.
				float p[2] = { surf_lims[0] + xa + xi * 2 * xa, surf_lims[3] - (ya + yi * 2 * ya) };

				int top_face_idx = -1;
				float top_face_z = std::numeric_limits<float>::infinity();

				//Find closest face.
				for (unsigned fi = 0; fi < faces.size() / 3; fi++) {
					float bc[3];
					if (compute_barycentric_coords(p, &pv[faces[fi * 3] * 3], &pv[faces[fi * 3 + 1] * 3], &pv[faces[fi * 3 + 2] * 3], bc)) {
						float z = 1.0f / (bc[0] / pv[faces[fi * 3] * 3 + 2] + bc[1] / pv[faces[fi * 3 + 1] * 3 + 2] + bc[2] / pv[faces[fi * 3 + 2] * 3 + 2]);
						if (z < top_face_z) {
							top_face_idx = fi;
							top_face_z = z;
						}
					}

					//If face found, set pixel colour.
					if (top_face_idx != -1) {
						res[yi*surf_res[0] + xi] = color[top_face_idx];
					}
				}
			}
		}
		return 0;
	}

public:

	SceneInfo(std::string const& config) {
		try {
			json j = json::parse(config);
			vertices = j.at("vertices").get<std::vector<float>>();
			faces = j.at("faces").get<std::vector<int>>();
			cam_pos = j.at("cam_pos").get<std::array<float, 3>>();
			cam_angle = j.at("cam_angle").get<std::array<float, 3>>();
			surf_pos = j.at("surf_pos").get<std::array<float, 3>>();
			surf_lims = j.at("surf_lims").get<std::array<float, 4>>();
			surf_res = j.at("surf_res").get<std::array<int, 2>>();

			for (auto it = j["color"].begin(); it != j["color"].end();) {
				DWORD c = *it;
				++it;
				c |= *it << 8;
				++it;
				c |= *it << 16;
				++it;
				color.push_back(c);
			}

			if ((vertices.size() % 3) != 0)
				throw std::runtime_error("Vertices");

			if ((faces.size() % 3) != 0)
				throw std::runtime_error("faces");
			for (auto it = faces.begin(); it != faces.end(); ++it) {
				if (*it >= vertices.size() / 3 || *it < 0)
					throw std::runtime_error("faces");
			}

			if (surf_res[0] < 1 || surf_res[1] < 1) {
				throw std::runtime_error("surf_res");
			}
		}
		catch (json::parse_error&) {
			throw std::runtime_error("Bad config, parse error");
		}
		catch (json::type_error&) {
			throw std::runtime_error("Bad config, type error");
		}
		catch (json::out_of_range&) {
			throw std::runtime_error("Bad config, out of range error");
		}
		catch (std::runtime_error& e) {
			throw std::runtime_error(std::string("Bad config. ") + e.what());
		}

		px.resize(surf_res[0] * surf_res[1]);

		if ((px_mutex = CreateMutex(0, false, L"draw_mutex")) == 0) {
			throw std::runtime_error("CreateMutex");
		}

		worker_running = true;

		if ((worker_thread = CreateThread(NULL, 0, draw_thread_entry, this, 0, 0)) == 0) {
			throw std::runtime_error("CreateThread");
		}

		compute_cam_matrix();
	}

	~SceneInfo() {
		worker_running = false;
		WaitForSingleObject(worker_thread, INFINITE);
		WaitForSingleObject(px_mutex, INFINITE);
		CloseHandle(px_mutex);
	}

	std::array<int, 2> const& get_surf_res() {
		return surf_res;
	}

	std::vector<COLORREF> const& get_px() {
		WaitForSingleObject(px_mutex, INFINITE);
		return px;
	}

	void release_px() {
		ReleaseMutex(px_mutex);
	}

	void incr_cam_pos(int ax, float amnt) {
		float v1[3] = { ax == 0,ax == 1,ax == 2 };
		float v2[3];
		Matrix33::mul_vec(&inv_cam_matrix[0], v1, v2);
		cam_pos[0] += v2[0] * amnt;
		cam_pos[1] += v2[1] * amnt;
		cam_pos[2] += v2[2] * amnt;
	}

	void incr_cam_angle(int ax, float amnt) {
		float u[9], newcam[9], newinvcam[9];
		Matrix33::compute_rotation_matrix(ax, -amnt, u);
		Matrix33::mul_mat(&cam_matrix[0], u, newcam);
		Matrix33::compute_rotation_matrix(ax, amnt, u);
		Matrix33::mul_mat(u, &cam_matrix[0], newinvcam);
		std::copy(newcam, newcam + 9, cam_matrix.begin());
		std::copy(newinvcam, newinvcam + 9, inv_cam_matrix.begin());
	}

	int test_gradient() {
		static int t = 0;
		for (int xi = 0; xi < surf_res[0]; xi++) {
			for (int yi = 0; yi < surf_res[1]; yi++) {
				COLORREF c = min(255, (int)(255.0f / surf_res[0] * xi / surf_res[1] * yi)) << (t * 8);
				px[yi*surf_res[1] + xi] = c;
			}
		}
		t = (t + 1) % 3;
		return 0;
	}
};



struct WindowVars {
	void* inst;
	bool destroyed;
	bool post_quit;
};


//Class containing all functionality for rendering and displaying.
//Window created and shown on construction, destroyed either when WM_DESTROY is normally received or on object destruction.
class DrawWindow {
	SceneInfo scene_info;
	HWND hwnd;
	HINSTANCE hInst;
	UINT_PTR timer_id;
	WindowVars* window_vars;

public:
	static LRESULT WINAPI window_proc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
		if (uMsg == WM_NCCREATE) {
			WindowVars* v = (WindowVars*)((LPCREATESTRUCT)lParam)->lpCreateParams;
			SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)v);
			return true;
		}

		WindowVars* v = (WindowVars*)GetWindowLongPtr(hwnd, GWLP_USERDATA);

		if (uMsg == WM_DESTROY) {
			if (v->post_quit)
				PostQuitMessage(0);
			delete v;
			return 0;
		}

		if (v && v->inst)
			return ((DrawWindow*)(v->inst))->process_message(hwnd, uMsg, wParam, lParam);

		return DefWindowProc(hwnd, uMsg, wParam, lParam);
	}

	LRESULT process_message(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
		switch (uMsg) {
		case WM_TIMER:
		{
			if (!hwnd)
				return DefWindowProc(hwnd, uMsg, wParam, lParam);

			std::vector<COLORREF> px = scene_info.get_px();
			HBITMAP b = CreateBitmap(scene_info.get_surf_res()[0], scene_info.get_surf_res()[1], 1, 32, &px[0]);
			HDC hdc = GetDC(hwnd);
			HDC s = CreateCompatibleDC(hdc);
			SelectObject(s, b);
			BitBlt(hdc, 0, 0, scene_info.get_surf_res()[0], scene_info.get_surf_res()[1], s, 0, 0, SRCCOPY);
			DeleteObject(b);
			DeleteDC(s);
			ReleaseDC(hwnd, hdc);
			scene_info.release_px();
			break;
		}

		case WM_KEYDOWN:
			switch (wParam) {
			case 0x41: //A
				scene_info.incr_cam_pos(0, -0.1f);
				break;
			case 0x44: //D
				scene_info.incr_cam_pos(0, 0.1f);
				break;
			case 0x57: //W
				scene_info.incr_cam_pos(1, 0.1f);
				break;
			case 0x53: //S
				scene_info.incr_cam_pos(1, -0.1f);
				break;
			case 0x58: //X
				scene_info.incr_cam_pos(2, -0.1f);
				break;
			case 0x43: //C
				scene_info.incr_cam_pos(2, 0.1f);
				break;
			case VK_LEFT:
				scene_info.incr_cam_angle(1, 0.02f);
				break;
			case VK_RIGHT:
				scene_info.incr_cam_angle(1, -0.02f);
				break;
			case VK_UP:
				scene_info.incr_cam_angle(0, 0.02f);
				break;
			case VK_DOWN:
				scene_info.incr_cam_angle(0, -0.02f);
				break;
			}
			break;

		default:
			return DefWindowProc(hwnd, uMsg, wParam, lParam);
		}
		return 0;
	}

	DrawWindow(std::string const& config, HINSTANCE hInst, bool post_quit) :
		scene_info(config),
		hInst(hInst),
		hwnd(NULL)
	{
		WNDCLASSEX wcx;
		ZeroMemory(&wcx, sizeof(wcx));
		wcx.cbSize = sizeof(wcx);
		wcx.style = CS_HREDRAW | CS_VREDRAW;
		wcx.lpfnWndProc = window_proc;
		wcx.hbrBackground = (HBRUSH)GetStockObject(WHITE_BRUSH);
		wcx.hInstance = hInst;
		wcx.lpszClassName = L"DrawWindowCls";
		if (!RegisterClassEx(&wcx))
			throw std::runtime_error("RegisterClassEx");

		RECT client_area = { 0,0,scene_info.get_surf_res()[0], scene_info.get_surf_res()[1] };
		AdjustWindowRectEx(&client_area, WS_OVERLAPPEDWINDOW, false, WS_EX_OVERLAPPEDWINDOW);

		window_vars = new WindowVars{ this, false, post_quit };

		hwnd = CreateWindowEx(WS_EX_OVERLAPPEDWINDOW,
			L"DrawWindowCls",
			L"Draw3D",
			WS_OVERLAPPEDWINDOW,
			CW_USEDEFAULT,
			CW_USEDEFAULT,
			client_area.right - client_area.left,
			client_area.bottom - client_area.top,
			NULL, NULL, hInst, window_vars);

		if (!hwnd)
			throw std::runtime_error("CreateWindowEx");

		if (!(timer_id = SetTimer(hwnd, 0, 100, NULL))) {
			SendMessage(hwnd, WM_DESTROY, 0, 0);
			hwnd = NULL;
			throw std::runtime_error("SetTimer");
		}

		ShowWindow(hwnd, SW_SHOW);
		UpdateWindow(hwnd);
	}

	~DrawWindow() {
		window_vars->inst = NULL;
		DestroyWindow(hwnd);
	}


};

int main(int argc, char* argv[]) {
	if (argc == 1)
		std::cout << "First argument can be a json config file. See comments for SceneInfo in draw3d.cpp." << std::endl;

	HINSTANCE hInst = GetModuleHandle(NULL);

	if (!hInst) {
		throw std::runtime_error("GetModuleHandle");
	}

	std::string config = TEST_CUBE;

	//std::cout << json::parse(config).dump(4) << std::endl;
	if (argc > 1) {
		std::cout << "Config " << argv[1] << std::endl;
		std::ifstream in(argv[1]);
		std::stringstream ss;
		ss << in.rdbuf();
		config = ss.str();
	}
	else {
		std::cout << "Config TEST_CUBE" << std::endl;
	}

	try {
		std::cout << json::parse(config).dump(4) << std::endl;
	}
	catch (json::parse_error) {
		std::cout << "Could not parse config file" << std::endl;
		return 0;
	}

	try {
		DrawWindow draw_window(config, hInst, true);

		MSG msg;
		while (GetMessage(&msg, NULL, 0, 0)) {
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
	}
	catch (std::runtime_error& e) {
		std::cout << e.what() << std::endl;
	}

	return 0;
}
