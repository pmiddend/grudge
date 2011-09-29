#include <iostream>
#include <fstream>
#include <sstream>

#include <CL/cl.h>
#include <CL/cl_gl.h>

#ifdef _WIN32
#include <windows.h>
#endif
//#include <GL/glew.h>
#include <GL/freeglut.h>

#ifdef __GNUC__
#include <GL/glx.h>
#endif

GLuint vbo = 0;
int const vbolen = 256; 
cl_device_id device;
cl_mem cl_vbo_mem;
cl_kernel kernel = 0;
cl_context context = 0;
cl_command_queue commandQueue = 0;
cl_program program = 0;
int const iterations = 8000;

char const kernel_string[] = 
	"__kernel void init_vbo_kernel(__global float4 *vbo) { vbo[get_global_id(0)] = 0.0f; }";

void Cleanup()
{
	if(commandQueue)
		clReleaseCommandQueue(commandQueue);

	if(kernel)
		clReleaseKernel(kernel);

	if(program)
		clReleaseProgram(program);

	if(context)
	{
		std::cerr << "Before releasing context\n";
		clReleaseContext(context);
		std::cerr << "After releasing context\n";
	}

	if(cl_vbo_mem) 
	{
		std::cerr << "Before releasing memory object\n";
		clReleaseMemObject(cl_vbo_mem);
		std::cerr << "After releasing memory object\n";
	}

	if(vbo)
	{
		glBindBuffer(GL_ARRAY_BUFFER_ARB, 0);
		glDeleteBuffers(1, &vbo);
	}

	exit(0);
}

void initVBO()
{
	GLint bsize;

	// generate the buffer
	glGenBuffers(1, &vbo);

	// bind the buffer 
	glBindBuffer(GL_ARRAY_BUFFER, vbo); 
	if(glGetError() != GL_NO_ERROR) {
		std::cerr << "Could not bind buffer" << std::endl;
		exit(0);
	}

	glBufferData(GL_ARRAY_BUFFER, vbolen*sizeof(float)*4, NULL, GL_STREAM_DRAW);  
	if(glGetError() != GL_NO_ERROR) {
		std::cerr<<"Could not bind buffer"<<std::endl;
		exit(0);
	}

	// recheck the size of the created buffer to make sure its what we requested
	glGetBufferParameteriv(GL_ARRAY_BUFFER, GL_BUFFER_SIZE, &bsize); 
	if ((GLuint)bsize != (vbolen*sizeof(float)*4)) {
		printf("Vertex Buffer object (%d) has incorrect size (%d).\n", (unsigned)vbo, (unsigned)bsize);
		exit(0);
	}

	// we're done, so unbind the buffers
	glBindBuffer(GL_ARRAY_BUFFER, 0);                    
	if(glGetError() != GL_NO_ERROR) {
		std::cerr<<"Could not bind buffer"<<std::endl;
		exit(0);
	}
}

void computeVBO()
{
	cl_int errNum;

	// Set the kernel arguments, send the cl_mem object for the VBO
	errNum = clSetKernelArg(kernel, 0, sizeof(cl_mem), &cl_vbo_mem);
	if (errNum != CL_SUCCESS)
	{
		std::cerr << "Error setting kernel arguments." << std::endl;
		Cleanup();
	}

	size_t globalWorkSize[1] = { vbolen };
	size_t localWorkSize[1] = { 32 };

	// Acquire the GL Object
	// Note, we should ensure GL is completed with any commands that might affect this VBO
	// before we issue OpenCL commands
	glFinish();
	errNum = clEnqueueAcquireGLObjects(commandQueue, 1, &cl_vbo_mem, 0, NULL, NULL );

	// Queue the kernel up for execution across the array
	for(int i = 0; i < iterations; ++i)
	{
		errNum = clEnqueueNDRangeKernel(commandQueue, kernel, 1, NULL,
																globalWorkSize, localWorkSize,
																0, NULL, NULL);
		if (errNum != CL_SUCCESS)
		{
			std::cerr << "Error queuing kernel for execution." << std::endl;
		}
	}

	// Release the GL Object
	// Note, we should ensure OpenCL is finished with any commands that might affect the VBO
	errNum = clEnqueueReleaseGLObjects(commandQueue, 1, &cl_vbo_mem, 0, NULL, NULL );
	clFinish(commandQueue);
	Cleanup();
}

///
//  Create an OpenCL context on the first available platform using
//  either a GPU or CPU depending on what is available.
//
void CreateContext()
{
	cl_int errNum;
	cl_uint numPlatforms;
	cl_platform_id firstPlatformId;

	errNum = clGetPlatformIDs(1, &firstPlatformId, &numPlatforms);
	if (errNum != CL_SUCCESS || numPlatforms <= 0)
	{
			std::cerr << "Failed to find any OpenCL platforms." << std::endl;
			return;
	}

	cl_context_properties contextProperties[] =
	{
#ifdef _WIN32
		CL_CONTEXT_PLATFORM,
		(cl_context_properties)firstPlatformId,
		CL_GL_CONTEXT_KHR,
		(cl_context_properties)wglGetCurrentContext(),
		CL_WGL_HDC_KHR,
		(cl_context_properties)wglGetCurrentDC(),
#elif defined( __GNUC__)
		CL_CONTEXT_PLATFORM, (cl_context_properties)firstPlatformId, 
		CL_GL_CONTEXT_KHR, (cl_context_properties)glXGetCurrentContext(), 
		CL_GLX_DISPLAY_KHR, (cl_context_properties)glXGetCurrentDisplay(), 
#else
#endif
		0
	};

	cl_uint uiDevCount;
	cl_device_id* cdDevices;
	errNum = clGetDeviceIDs(firstPlatformId, CL_DEVICE_TYPE_GPU, 0, NULL, &uiDevCount);

	cdDevices = new cl_device_id [uiDevCount];
	errNum = clGetDeviceIDs(firstPlatformId, CL_DEVICE_TYPE_GPU, uiDevCount, cdDevices, NULL);

	context = clCreateContext(contextProperties, 1, &cdDevices[0], NULL, NULL, &errNum);

	if (errNum != CL_SUCCESS)
	{
		std::cerr << "Failed to create an OpenCL GPU or CPU context." << std::endl;
		return;
	}
}

void CreateCommandQueue()
{
    cl_int errNum;
    cl_device_id *devices;
    size_t deviceBufferSize = -1;

    errNum = clGetContextInfo(context, CL_CONTEXT_DEVICES, 0, NULL, &deviceBufferSize);
    if (errNum != CL_SUCCESS)
    {
        std::cerr << "Failed call to clGetContextInfo(...,GL_CONTEXT_DEVICES,...)";
        return;
    }

    if (deviceBufferSize <= 0)
    {
        std::cerr << "No devices available.";
        return;
    }

    devices = new cl_device_id[deviceBufferSize / sizeof(cl_device_id)];
    errNum = clGetContextInfo(context, CL_CONTEXT_DEVICES, deviceBufferSize, devices, NULL);
    if (errNum != CL_SUCCESS)
    {
        std::cerr << "Failed to get device IDs";
        return;
    }

    commandQueue = clCreateCommandQueue(context, devices[0], 0, NULL);
    if (commandQueue == NULL)
    {
        std::cerr << "Failed to create commandQueue for device 0";
        return;
    }

    device = devices[0];
    delete [] devices;
}

void CreateProgram()
{
	cl_int errNum;

	char const * strings[] = { kernel_string };
	program = clCreateProgramWithSource(context, 1, strings, 0, 0);
	if (program == NULL)
	{
		std::cerr << "Failed to create CL program from source." << std::endl;
		return;
	}

	errNum = clBuildProgram(program, 0, NULL, NULL, NULL, NULL);
	if (errNum != CL_SUCCESS)
	{
		char buildLog[16384];
		clGetProgramBuildInfo(program, device, CL_PROGRAM_BUILD_LOG,
													sizeof(buildLog), buildLog, NULL);

		std::cerr << "Error in kernel: " << std::endl;
		std::cerr << buildLog;
		clReleaseProgram(program);
		return;
	}
}

bool CreateMemObjects()
{
	cl_int errNum;

	cl_vbo_mem = clCreateFromGLBuffer(context, CL_MEM_READ_WRITE, vbo, &errNum );
	if(errNum != CL_SUCCESS)
	{
		std::cerr<< "Failed creating memory from GL buffer." << std::endl;
		return false;
	}
	
	return true;
}


int main(int argc, char** argv)
{
	glutInit(&argc, argv);
	glutInitDisplayMode(GLUT_DOUBLE | GLUT_RGBA);
	glutCreateWindow("GL interop");
	glutIconifyWindow();
	glutDisplayFunc(computeVBO);
	glutIdleFunc(computeVBO);
	//glewInit();

	initVBO();

	// Create an OpenCL context on first available platform
	CreateContext();
	if(!context)
	{
		std::cerr << "Failed to create OpenCL context." << std::endl;
		return EXIT_FAILURE;
	}

	CreateCommandQueue();
	if(!commandQueue)
	{
		Cleanup();
		return EXIT_FAILURE;
	}

	CreateProgram();
	if(!program)
	{
		Cleanup();
		return EXIT_FAILURE;
	}

	// Create OpenCL kernel
	kernel = clCreateKernel(program, "init_vbo_kernel", NULL);
	if(!kernel)
	{
		std::cerr << "Failed to create kernel" << std::endl;
		Cleanup();
		return EXIT_FAILURE;
	}

	// Create memory objects that will be used as arguments to
	// kernel
	if (!CreateMemObjects())
	{
		Cleanup();
		return EXIT_FAILURE;
	}

	glutMainLoop();

	std::cout << std::endl;
	std::cout << "Executed program succesfully." << std::endl;
	Cleanup();
}
