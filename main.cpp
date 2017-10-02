#include "load_save_png.hpp"
#include "GL.hpp"
#include "Meshes.hpp"
#include "Scene.hpp"
#include "Draw.hpp"
#include "read_chunk.hpp"

#include <SDL.h>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <chrono>
#include <iostream>
#include <stdexcept>
#include <fstream>

static GLuint compile_shader(GLenum type, std::string const &source);
static GLuint link_program(GLuint vertex_shader, GLuint fragment_shader);

int main(int argc, char **argv) {
	//Configuration:
	struct {
		std::string title = "Game2: Robot Fun Police";
		glm::uvec2 size = glm::uvec2(800, 600);
	} config;

	//------------  initialization ------------

	//Initialize SDL library:
	SDL_Init(SDL_INIT_VIDEO);

	//Ask for an OpenGL context version 3.3, core profile, enable debug:
	SDL_GL_ResetAttributes();
	SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
	SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
	SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
	SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 8);
	SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
	SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
	SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_DEBUG_FLAG);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);

	//create window:
	SDL_Window *window = SDL_CreateWindow(
		config.title.c_str(),
		SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
		config.size.x, config.size.y,
		SDL_WINDOW_OPENGL /*| SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI*/
	);

	if (!window) {
		std::cerr << "Error creating SDL window: " << SDL_GetError() << std::endl;
		return 1;
	}

	//Create OpenGL context:
	SDL_GLContext context = SDL_GL_CreateContext(window);

	if (!context) {
		SDL_DestroyWindow(window);
		std::cerr << "Error creating OpenGL context: " << SDL_GetError() << std::endl;
		return 1;
	}

	#ifdef _WIN32
	//On windows, load OpenGL extensions:
	if (!init_gl_shims()) {
		std::cerr << "ERROR: failed to initialize shims." << std::endl;
		return 1;
	}
	#endif

	//Set VSYNC + Late Swap (prevents crazy FPS):
	if (SDL_GL_SetSwapInterval(-1) != 0) {
		std::cerr << "NOTE: couldn't set vsync + late swap tearing (" << SDL_GetError() << ")." << std::endl;
		if (SDL_GL_SetSwapInterval(1) != 0) {
			std::cerr << "NOTE: couldn't set vsync (" << SDL_GetError() << ")." << std::endl;
		}
	}

	//Hide mouse cursor (note: showing can be useful for debugging):
	//SDL_ShowCursor(SDL_DISABLE);

	//------------ opengl objects / game assets ------------

	//shader program:
	GLuint program = 0;
	GLuint program_Position = 0;
	GLuint program_Normal = 0;
	GLuint program_Color = 0;
	GLuint program_mvp = 0;
	GLuint program_itmv = 0;
	GLuint program_to_light = 0;
	{ //compile shader program:
		GLuint vertex_shader = compile_shader(GL_VERTEX_SHADER,
			"#version 330\n"
			"uniform mat4 mvp;\n"
			"uniform mat3 itmv;\n"
			"in vec4 Position;\n"
			"in vec3 Normal;\n"
			"in vec3 Color;\n"
			"out vec3 normal;\n"
			"out vec3 color;\n"
			"void main() {\n"
			"	gl_Position = mvp * Position;\n"
			"	normal = itmv * Normal;\n"
			"	color = Color;\n"
			"}\n"
		);

		GLuint fragment_shader = compile_shader(GL_FRAGMENT_SHADER,
			"#version 330\n"
			"uniform vec3 to_light;\n"
			"in vec3 normal;\n"
			"in vec3 color;\n"
			"out vec4 fragColor;\n"
			"void main() {\n"
			"	float nl = dot(normalize(normal), to_light);\n"
			"   vec3 ambience = color * 0.5f;\n"
			"	fragColor = vec4(ambience + (color / 3.1415926) * 1.5f * (smoothstep(0.0, 0.1, nl) * 0.6 + 0.4), 1.0);\n"
			"}\n"
		);

		program = link_program(fragment_shader, vertex_shader);

		//look up attribute locations:
		program_Position = glGetAttribLocation(program, "Position");
		if (program_Position == -1U) throw std::runtime_error("no attribute named Position");
		program_Normal = glGetAttribLocation(program, "Normal");
		if (program_Normal == -1U) throw std::runtime_error("no attribute named Normal");
		program_Color = glGetAttribLocation(program, "Color");
		if (program_Color == -1U) throw std::runtime_error("no attribute named Color");

		//look up uniform locations:
		program_mvp = glGetUniformLocation(program, "mvp");
		if (program_mvp == -1U) throw std::runtime_error("no uniform named mvp");
		program_itmv = glGetUniformLocation(program, "itmv");
		if (program_itmv == -1U) throw std::runtime_error("no uniform named itmv");

		program_to_light = glGetUniformLocation(program, "to_light");
		if (program_to_light == -1U) throw std::runtime_error("no uniform named to_light");
	}

	//------------ meshes ------------

	Meshes meshes;

	{ //add meshes to database:
		Meshes::Attributes attributes;
		attributes.Position = program_Position;
		attributes.Normal = program_Normal;
		attributes.Color = program_Color;

		meshes.load("meshes.blob", attributes);
	}
	
	//------------ scene ------------

	Scene scene;
	//set up camera parameters based on window:
	scene.camera.fovy = glm::radians(60.0f);
	scene.camera.aspect = float(config.size.x) / float(config.size.y);
	scene.camera.near = 0.01f;
	//(transform will be handled in the update function below)

	std::vector< Scene::Object * > pool;

	//add some objects from the mesh library:
	auto add_object = [&](std::string const &name, glm::vec3 const &position, glm::quat const &rotation, glm::vec3 const &scale) -> Scene::Object & {
		Mesh const &mesh = meshes.get(name);
		scene.objects.emplace_back();
		Scene::Object &object = scene.objects.back();
		object.transform.position = position;
		object.transform.rotation = rotation;
		object.transform.scale = scale;
		object.vao = mesh.vao;
		object.start = mesh.start;
		object.count = mesh.count;
		object.program = program;
		object.program_mvp = program_mvp;
		object.program_itmv = program_itmv;
		pool.emplace_back(&object);
		return object;
	};


	{ //read objects to add from "scene.blob":
		std::ifstream file("scene.blob", std::ios::binary);

		std::vector< char > strings;
		//read strings chunk:
		read_chunk(file, "str0", &strings);

		{ //read scene chunk, add meshes to scene:
			struct SceneEntry {
				uint32_t name_begin, name_end;
				glm::vec3 position;
				glm::quat rotation;
				glm::vec3 scale;
			};
			static_assert(sizeof(SceneEntry) == 48, "Scene entry should be packed");

			std::vector< SceneEntry > data;
			read_chunk(file, "scn0", &data);

			for (auto const &entry : data) {
				if (!(entry.name_begin <= entry.name_end && entry.name_end <= strings.size())) {
					throw std::runtime_error("index entry has out-of-range name begin/end");
				}
				std::string name(&strings[0] + entry.name_begin, &strings[0] + entry.name_end);
				add_object(name, entry.position, entry.rotation, entry.scale);
			}
		}
	}

	struct Body {
		glm::vec2 pos;
		glm::vec2 dir = glm::vec2(0.0f, 0.0f);
		glm::quat rot = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
		float vel = 0.0f;
	} * bodies[17];

	struct Ball {
		Scene::Object* obj;
		Body body;
		enum {
			SOLID,
			DIAMOND,
			EIGHT
		} type;
		bool sunk = false;
	} balls[15];

	for (int i = 0; i < 15; i++) {
		balls[i].obj = pool[i];
		balls[i].body.pos = pool[i]->transform.position;
		if (i < 7)
			balls[i].type = Ball::SOLID;
		else if (i == 7)
			balls[i].type = Ball::EIGHT;
		else
			balls[i].type = Ball::DIAMOND;
	}

	static const float MAX_POWER = 10.0f;
	static const float MIN_POWER = 1.0f;
	
	struct Dozer {
		Scene::Object* obj;
		Body body;
		float power = MIN_POWER;
		float rad = 0.0f;
	} dozers[2];

	dozers[0].obj = pool[15];
	dozers[0].body.pos = pool[15]->transform.position;
	dozers[0].body.rot = pool[15]->transform.rotation;
	dozers[0].rad = (float)M_PI;
	dozers[1].obj = pool[16];
	dozers[1].body.pos = pool[16]->transform.position;

	for (int i = 0; i < 15; i++)
		bodies[i] = &balls[i].body;
	bodies[15] = &dozers[0].body;
	bodies[16] = &dozers[1].body;

	int solids = 7;
	int diamonds = 7;
	bool solid = false;
	bool diamond = false;
	bool eight = false;

	glm::vec2 mouse = glm::vec2(0.0f, 0.0f); //mouse position in [-1,1]x[-1,1] coordinates

	struct {
		float radius = 5.2f;
		float elevation = 1.2f;
		float azimuth = 0.5f * M_PI;
		glm::vec3 target = glm::vec3(0.0f, 0.0f, 0.0f);
	} camera;

	enum {
		SOLID,
		SOLID_RESOLVING,
		DIAMOND,
		DIAMOND_RESOLVING,
		SOLID_WIN,
		DIAMOND_WIN
	} turn = DIAMOND;

	//------------ game loop ------------

	bool should_quit = false;
	while (true) {
		static SDL_Event evt;
		while (SDL_PollEvent(&evt) == 1) {
			//handle input:
			if (evt.type == SDL_MOUSEMOTION) {
				glm::vec2 old_mouse = mouse;
				mouse.x = (evt.motion.x + 0.5f) / float(config.size.x) * 2.0f - 1.0f;
				mouse.y = (evt.motion.y + 0.5f) / float(config.size.y) *-2.0f + 1.0f;
				if (evt.motion.state & SDL_BUTTON(SDL_BUTTON_LEFT)) {
					camera.elevation += -2.0f * (mouse.y - old_mouse.y);
					camera.azimuth += -2.0f * (mouse.x - old_mouse.x);
				}
			} else if (evt.type == SDL_KEYDOWN && evt.key.keysym.sym == SDLK_ESCAPE) {
				should_quit = true;
			} else if (evt.type == SDL_QUIT) {
				should_quit = true;
				break;
			}
		}
		if (should_quit) break;

		auto current_time = std::chrono::high_resolution_clock::now();
		static auto previous_time = current_time;
		float elapsed = std::chrono::duration< float >(current_time - previous_time).count();
		previous_time = current_time;

		if (elapsed > 0.016f) {
			elapsed = 0.016f;
		}

		{ //update game state:
			static const Uint8* state = SDL_GetKeyboardState(NULL);

			if (turn == DIAMOND) {
				if (state[SDL_SCANCODE_A]) {
					dozers[0].rad += elapsed * M_PI;
					dozers[0].body.rot = glm::quat(glm::vec3(0.0f, 0.0f, dozers[0].rad));
				}
				if (state[SDL_SCANCODE_D]) {
					dozers[0].rad -= elapsed * M_PI;
					dozers[0].body.rot = glm::quat(glm::vec3(0.0f, 0.0f, dozers[0].rad));
				}
				if (state[SDL_SCANCODE_W]) {
					dozers[0].power += MAX_POWER * elapsed;
					if (dozers[0].power >= MAX_POWER)
						dozers[0].power = MAX_POWER;
				}
				if (state[SDL_SCANCODE_S]) {
					dozers[0].power -= MAX_POWER * elapsed;
					if (dozers[0].power <= MIN_POWER)
						dozers[0].power = MIN_POWER;
				}
				if (state[SDL_SCANCODE_LSHIFT]) {
					dozers[0].body.dir.x = glm::cos(dozers[0].rad);
					dozers[0].body.dir.y = glm::sin(dozers[0].rad);
					dozers[0].body.vel = dozers[0].power;
					solid = false;
					diamond = false;
					turn = DIAMOND_RESOLVING;
				}
			} else if (turn == SOLID) {
				if (state[SDL_SCANCODE_LEFT]) {
					dozers[1].rad += elapsed * M_PI;
					dozers[1].body.rot = glm::quat(glm::vec3(0.0f, 0.0f, dozers[1].rad));
				}
				if (state[SDL_SCANCODE_RIGHT]) {
					dozers[1].rad -= elapsed * M_PI;
					dozers[1].body.rot = glm::quat(glm::vec3(0.0f, 0.0f, dozers[1].rad));
				}
				if (state[SDL_SCANCODE_UP]) {
					dozers[1].power += MAX_POWER * elapsed;
					if (dozers[1].power >= MAX_POWER)
						dozers[1].power = MAX_POWER;
				}
				if (state[SDL_SCANCODE_DOWN]) {
					dozers[1].power -= MAX_POWER * elapsed;
					if (dozers[1].power <= MIN_POWER)
						dozers[1].power = MIN_POWER;
				}
				if (state[SDL_SCANCODE_RSHIFT]) {
					dozers[1].body.dir.x = glm::cos(dozers[1].rad);
					dozers[1].body.dir.y = glm::sin(dozers[1].rad);
					dozers[1].body.vel = dozers[1].power;
					solid = false;
					diamond = false;
					turn = SOLID_RESOLVING;
				}
			}

			// update bodies
			if (turn == SOLID_RESOLVING || turn == DIAMOND_RESOLVING) {
				const float BALL_LIFE = 1.0f / 10.0f;
				const float BALL_AIR = 0.05f;
				const float DOZER_LIFE = 1.0f / 5.0f;
				const float DOZER_AIR = 0.5f;
				for (float temp = elapsed; temp > 0.0f; temp -= 0.001f) {
					float step = temp < 0.001f ? temp : 0.001f;
					// position update
					for (int i = 0; i < 17; i++) {
						if (bodies[i]->vel > 0.0f) {
							bodies[i]->pos += step * bodies[i]->vel * bodies[i]->dir;
						}
					}
					// rotation update
					for (int i = 0; i < 15; i++) {
						if (bodies[i]->vel > 0.0f) {
							float m = 0.5f * step * bodies[i]->vel / 0.15f;
							float sinm = glm::sin(m);
							glm::quat rot = glm::quat(glm::cos(m), -bodies[i]->dir.y * sinm, bodies[i]->dir.x * sinm, 0.0f);
							bodies[i]->rot = rot * bodies[i]->rot;
						}
					}
					// bad collision checks and resolutions incoming
					// wall check for balls
					for (int i = 0; i < 15; i++) {
						// badly approximate resolution
						if (!balls[i].sunk) {
							if (bodies[i]->pos.x < -2.85f) {
								bodies[i]->pos.x = -2.85f;
								if (bodies[i]->dir.x < 0.0f)
									bodies[i]->dir.x = -bodies[i]->dir.x;
								if (bodies[i]->pos.y < -1.6f || bodies[i]->pos.y > 1.6f) {
									bodies[i]->vel = 0.0f;
									balls[i].sunk = true;
									switch (balls[i].type) {
									case Ball::SOLID:
										solid = true;
										solids--;
										break;
									case Ball::DIAMOND:
										diamond = true;
										diamonds--;
										break;
									case Ball::EIGHT:
										eight = true;
									}
								}
							} else if (bodies[i]->pos.x > 2.85f) {
								bodies[i]->pos.x = 2.85f;
								if (bodies[i]->dir.x > 0.0f)
									bodies[i]->dir.x = -bodies[i]->dir.x;
								if (bodies[i]->pos.y < -1.6f || bodies[i]->pos.y > 1.6f) {
									bodies[i]->vel = 0.0f;
									balls[i].sunk = true;
									switch (balls[i].type) {
									case Ball::SOLID:
										solid = true;
										solids--;
										break;
									case Ball::DIAMOND:
										diamond = true;
										diamonds--;
										break;
									case Ball::EIGHT:
										eight = true;
									}
								}
							}
							if (bodies[i]->pos.y < -1.85f) {
								bodies[i]->pos.y = -1.85f;
								if (bodies[i]->dir.y < 0.0f)
									bodies[i]->dir.y = -bodies[i]->dir.y;
								if (-0.4f < bodies[i]->pos.x && bodies[i]->pos.x < 0.4f) {
									bodies[i]->vel = 0.0f;
									balls[i].sunk = true;
									switch (balls[i].type) {
									case Ball::SOLID:
										solid = true;
										solids--;
										break;
									case Ball::DIAMOND:
										diamond = true;
										diamonds--;
										break;
									case Ball::EIGHT:
										eight = true;
									}
								}
							} else if (bodies[i]->pos.y > 1.85f) {
								bodies[i]->pos.y = 1.85f;
								if (bodies[i]->dir.y > 0.0f)
									bodies[i]->dir.y= -bodies[i]->dir.y;
								if (-0.4f < bodies[i]->pos.x && bodies[i]->pos.x < 0.4f) {
									bodies[i]->vel = 0.0f;
									balls[i].sunk = true;
									switch (balls[i].type) {
									case Ball::SOLID:
										solid = true;
										solids--;
										break;
									case Ball::DIAMOND:
										diamond = true;
										diamonds--;
										break;
									case Ball::EIGHT:
										eight = true;
									}
								}
							}
						}
					}
					for (int i = 0; i < 2; i++) {
						// badly approximate resolution
						if (bodies[i + 15]->pos.x < -2.85f) {
							bodies[i + 15]->pos.x = -2.85f;
							if (bodies[i + 15]->dir.x < 0.0f)
								bodies[i + 15]->dir.x = -bodies[i + 15]->dir.x;
							bodies[i + 15]->vel *= 0.1f;
						}
						else if (bodies[i + 15]->pos.x > 2.85f) {
							bodies[i + 15]->pos.x = 2.85f;
							if (bodies[i + 15]->dir.x > 0.0f)
								bodies[i + 15]->dir.x = -bodies[i + 15]->dir.x;
							bodies[i + 15]->vel *= 0.1f;
						}
						if (bodies[i + 15]->pos.y < -1.85f) {
							bodies[i + 15]->pos.y = -1.85f;
							if (bodies[i + 15]->dir.y < 0.0f)
								bodies[i + 15]->dir.y = -bodies[i + 15]->dir.y;
							bodies[i + 15]->vel *= 0.1f;
						}
						else if (bodies[i + 15]->pos.y > 1.85f) {
							bodies[i + 15]->pos.y = 1.85f;
							if (bodies[i + 15]->dir.y > 0.0f)
								bodies[i + 15]->dir.y = -bodies[i + 15]->dir.y;
							bodies[i + 15]->vel *= 0.1f;
						}
					}
					// collision check
					for (int i = 0; i < 16; i++) {
						if (i < 15 && balls[i].sunk)
							continue;
						for (int j = i + 1; j < 17; j++) {
							if (i < 15 && balls[i].sunk)
								continue;
							glm::vec2 ab = bodies[i]->pos - bodies[j]->pos;
							if (ab.x * ab.x + ab.y * ab.y < 0.3f * 0.3f) {
								float length = glm::length(ab);
								glm::vec2 norm = ab / length;
								glm::vec2 res = 0.5f * (0.3f - length) * norm;
								// badly approximate resolution
								bodies[i]->pos += res;
								bodies[j]->pos -= res;
								// elastic collision w/ equal mass
								glm::vec2 a = bodies[j]->vel * bodies[j]->dir;
								glm::vec2 b = bodies[i]->vel * bodies[i]->dir;
								float ua = glm::dot(a, norm);
								float ub = glm::dot(b, norm);
								a -= (ua - ub) * norm;
								b += (ua - ub) * norm;
								bodies[j]->vel = glm::length(a);
								bodies[j]->dir = a / bodies[j]->vel;
								bodies[i]->vel = glm::length(b);
								bodies[i]->dir = b / bodies[i]->vel;
							}
						}
					}
					// velocity update
					for (int i = 0; i < 15; i++) {
						if (balls[i].body.vel > 0.0f) {
							balls[i].body.vel *= exp2f(-elapsed * BALL_LIFE);
							balls[i].body.vel -= step * BALL_AIR;
							if (balls[i].body.vel < 0.0f)
								balls[i].body.vel = 0.0f;
						}
					}
					for (int i = 0; i < 2; i++) {
						if (dozers[i].body.vel > 0.0f) {
							dozers[i].body.vel *= exp2f(-elapsed * DOZER_LIFE);
							dozers[i].body.vel -= step * DOZER_AIR;
							if (dozers[i].body.vel < 0.0f)
								dozers[i].body.vel = 0.0f;
						}
					}
				}

				// resolution check
				bool resolved = true;
				for (int i = 0; i < 17; i++) {
					if (bodies[i]->vel > 0.0f) {
						resolved = false;
						break;
					}
				}
				if (resolved) {
					for (int i = 0; i < 15; i++) {
						if (!balls[i].sunk) {
							// tired at this point.....
							if (glm::length(bodies[i]->pos - glm::vec2(-3.0f, -2.0f)) < 0.4f ||
								glm::length(bodies[i]->pos - glm::vec2(0.0f, -2.0f)) < 0.4f ||
								glm::length(bodies[i]->pos - glm::vec2(3.0f, -2.0f)) < 0.4f ||
								glm::length(bodies[i]->pos - glm::vec2(-3.0f, 2.0f)) < 0.4f ||
								glm::length(bodies[i]->pos - glm::vec2(0.0f, 2.0f)) < 0.4f ||
								glm::length(bodies[i]->pos - glm::vec2(3.0f, 2.0f)) < 0.4f) {
								if (-0.4f < bodies[i]->pos.x && bodies[i]->pos.x < 0.4f) {
									bodies[i]->vel = 0.0f;
									balls[i].sunk = true;
									switch (balls[i].type) {
									case Ball::SOLID:
										solid = true;
										solids--;
										break;
									case Ball::DIAMOND:
										diamond = true;
										diamonds--;
										break;
									case Ball::EIGHT:
										eight = true;
									}
								}
							}
						}
					}
					if (turn == SOLID_RESOLVING) {
						if (eight) {
							if (solids == 0) {
								turn = SOLID_WIN;
								std::cout << "Eight ball was sunk by solids. Solids wins!" << std::endl;
							} else {
								turn = DIAMOND_WIN;
								std::cout << "Eight ball was sunk early by solids. Diamonds wins!" << std::endl;
							}
						} else if (solid && !diamond)
							turn = SOLID;
						else {
							turn = DIAMOND;
						}
					} else if (turn == DIAMOND_RESOLVING) {
						if (eight) {
							if (diamonds == 0) {
								turn = DIAMOND_WIN;
								std::cout << "Eight ball was sunk by diamonds. Diamonds wins!" << std::endl;
							}
							else {
								turn = SOLID_WIN;
								std::cout << "Eight ball was sunk early by diamonds. Solids wins!" << std::endl;
							}
						} else if (diamond && !solid)
							turn = DIAMOND;
						else {
							turn = SOLID;
						}
					}
				}
			}

			// update objects
			for (int i = 0; i < 15; i++)
			{
				if (!balls[i].sunk) {
					balls[i].obj->transform.position = glm::vec3(balls[i].body.pos, 0.15f);
					balls[i].obj->transform.rotation = balls[i].body.rot;
				} else if (balls[i].obj->transform.position.z > 0.0f) {
					balls[i].obj->transform.position.z += elapsed * 10.0f;
					if (balls[i].obj->transform.position.z > 10.0f)
						balls[i].obj->transform.position.z = -0.15f;
				}
			}
			dozers[0].obj->transform.position = glm::vec3(dozers[0].body.pos, 0.0f);
			dozers[0].obj->transform.rotation = dozers[0].body.rot;
			dozers[1].obj->transform.position = glm::vec3(dozers[1].body.pos, 0.0f);
			dozers[1].obj->transform.rotation = dozers[1].body.rot;

			//camera:
			scene.camera.transform.position = camera.radius * glm::vec3(
				std::cos(camera.elevation) * std::cos(camera.azimuth),
				std::cos(camera.elevation) * std::sin(camera.azimuth),
				std::sin(camera.elevation)) + camera.target;

			glm::vec3 out = -glm::normalize(camera.target - scene.camera.transform.position);
			glm::vec3 up = glm::vec3(0.0f, 0.0f, 1.0f);
			up = glm::normalize(up - glm::dot(up, out) * out);
			glm::vec3 right = glm::cross(up, out);
			
			scene.camera.transform.rotation = glm::quat_cast(
				glm::mat3(right, up, out)
			);
			scene.camera.transform.scale = glm::vec3(1.0f, 1.0f, 1.0f);
		}

		//draw output:
		glClearColor(0.5, 0.5, 0.5, 0.0);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
		glEnable(GL_DEPTH_TEST);
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);


		{ //draw game state:
			glUseProgram(program);
			
			// lights. camera. action
			glm::vec3 light_in_camera = glm::mat3(scene.camera.transform.make_world_to_local()) * glm::vec3(0.0f, 0.0f, 1.0f);
			glUniform3fv(program_to_light, 1, glm::value_ptr(glm::normalize(light_in_camera)));
			scene.render();

			// ui
			Draw draw;
			for (int i = 0; i < 10; i++) {
				draw.add_rectangle(glm::vec2(-0.95f, -0.880f + 0.05f * i), glm::vec2(-0.80f, -0.870f + 0.05f * i), glm::vec4(0x00, 0x00, 0x00, 0xFF));
				draw.add_rectangle(glm::vec2(0.80f, -0.880f + 0.05f * i), glm::vec2(0.95f, -0.870f + 0.05f * i), glm::vec4(0x00, 0x00, 0x00, 0xFF));
			}
			draw.add_rectangle(glm::vec2(0.805f, -0.945f), glm::vec2(0.815f, -0.935f), glm::vec4(0xFF, 0xFF, 0xFF, 0xFF));
			draw.add_rectangle(glm::vec2(0.935f, -0.945f), glm::vec2(0.945f, -0.935f), glm::vec4(0xFF, 0xFF, 0xFF, 0xFF));
			draw.vertices.emplace_back(glm::vec2(-0.950f, -0.945f), glm::vec4(0xFF, 0xFF, 0xFF, 0xFF));
			draw.vertices.emplace_back(glm::vec2(-0.930f, -0.945f), glm::vec4(0xFF, 0xFF, 0xFF, 0xFF));
			draw.vertices.emplace_back(glm::vec2(-0.940f, -0.930f), glm::vec4(0xFF, 0xFF, 0xFF, 0xFF));
			draw.vertices.emplace_back(glm::vec2(-0.820f, -0.945f), glm::vec4(0xFF, 0xFF, 0xFF, 0xFF));
			draw.vertices.emplace_back(glm::vec2(-0.800f, -0.945f), glm::vec4(0xFF, 0xFF, 0xFF, 0xFF));
			draw.vertices.emplace_back(glm::vec2(-0.810f, -0.930f), glm::vec4(0xFF, 0xFF, 0xFF, 0xFF));
			switch (turn) {
			case DIAMOND:
				draw.add_rectangle(glm::vec2(-0.93f, -0.925f), glm::vec2(-0.82f, -0.925f + 0.05f * dozers[0].power), glm::vec4(0x00, 0xFF, 0x00, 0xFF));
				draw.add_rectangle(glm::vec2(0.82f, -0.925f), glm::vec2(0.93f, -0.925f + 0.05f * dozers[1].power), glm::vec4(0x80, 0x80, 0x80, 0xFF));
				break;
			case SOLID:
				draw.add_rectangle(glm::vec2(-0.93f, -0.925f), glm::vec2(-0.82f, -0.925f + 0.05f * dozers[0].power), glm::vec4(0x80, 0x80, 0x80, 0xFF));
				draw.add_rectangle(glm::vec2(0.82f, -0.925f), glm::vec2(0.93f, -0.925f + 0.05f * dozers[1].power), glm::vec4(0x00, 0xFF, 0x00, 0xFF));
				break;
			case DIAMOND_RESOLVING:
			case SOLID_RESOLVING:
				draw.add_rectangle(glm::vec2(-0.93f, -0.925f), glm::vec2(-0.82f, -0.925f + 0.05f * dozers[0].power), glm::vec4(0x80, 0x80, 0x80, 0xFF));
				draw.add_rectangle(glm::vec2(0.82f, -0.925f), glm::vec2(0.93f, -0.925f + 0.05f * dozers[1].power), glm::vec4(0x80, 0x80, 0x80, 0xFF));
				break;
			case DIAMOND_WIN:
				draw.add_rectangle(glm::vec2(-0.93f, -0.925f), glm::vec2(-0.82f, -0.925f + 0.05f * dozers[0].power), glm::vec4(0xFF, 0xFF, 0x00, 0xFF));
				draw.add_rectangle(glm::vec2(0.82f, -0.925f), glm::vec2(0.93f, -0.925f + 0.05f * dozers[1].power), glm::vec4(0xFF, 0x00, 0x00, 0xFF));
				break;
			case SOLID_WIN:
				draw.add_rectangle(glm::vec2(-0.93f, -0.925f), glm::vec2(-0.82f, -0.925f + 0.05f * dozers[0].power), glm::vec4(0xFF, 0x00, 0x00, 0xFF));
				draw.add_rectangle(glm::vec2(0.82f, -0.925f), glm::vec2(0.93f, -0.925f + 0.05f * dozers[1].power), glm::vec4(0xFF, 0xFF, 0x00, 0xFF));
				break;
			}
			draw.add_rectangle(glm::vec2(-0.95f, -0.95f), glm::vec2(-0.80f, -0.40f), glm::vec4(0x00, 0x00, 0x00, 0xFF));
			draw.add_rectangle(glm::vec2(0.80f, -0.95f), glm::vec2(0.95f, -0.40f), glm::vec4(0x00, 0x00, 0x00, 0xFF));

			draw.draw();
		}


		SDL_GL_SwapWindow(window);
	}


	//------------  teardown ------------

	SDL_GL_DeleteContext(context);
	context = 0;

	SDL_DestroyWindow(window);
	window = NULL;

	return 0;
}



static GLuint compile_shader(GLenum type, std::string const &source) {
	GLuint shader = glCreateShader(type);
	GLchar const *str = source.c_str();
	GLint length = source.size();
	glShaderSource(shader, 1, &str, &length);
	glCompileShader(shader);
	GLint compile_status = GL_FALSE;
	glGetShaderiv(shader, GL_COMPILE_STATUS, &compile_status);
	if (compile_status != GL_TRUE) {
		std::cerr << "Failed to compile shader." << std::endl;
		GLint info_log_length = 0;
		glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &info_log_length);
		std::vector< GLchar > info_log(info_log_length, 0);
		GLsizei length = 0;
		glGetShaderInfoLog(shader, info_log.size(), &length, &info_log[0]);
		std::cerr << "Info log: " << std::string(info_log.begin(), info_log.begin() + length);
		glDeleteShader(shader);
		throw std::runtime_error("Failed to compile shader.");
	}
	return shader;
}

static GLuint link_program(GLuint fragment_shader, GLuint vertex_shader) {
	GLuint program = glCreateProgram();
	glAttachShader(program, vertex_shader);
	glAttachShader(program, fragment_shader);
	glLinkProgram(program);
	GLint link_status = GL_FALSE;
	glGetProgramiv(program, GL_LINK_STATUS, &link_status);
	if (link_status != GL_TRUE) {
		std::cerr << "Failed to link shader program." << std::endl;
		GLint info_log_length = 0;
		glGetProgramiv(program, GL_INFO_LOG_LENGTH, &info_log_length);
		std::vector< GLchar > info_log(info_log_length, 0);
		GLsizei length = 0;
		glGetProgramInfoLog(program, info_log.size(), &length, &info_log[0]);
		std::cerr << "Info log: " << std::string(info_log.begin(), info_log.begin() + length);
		throw std::runtime_error("Failed to link program");
	}
	return program;
}
