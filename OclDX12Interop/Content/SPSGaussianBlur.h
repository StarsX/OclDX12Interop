//--------------------------------------------------------------------------------------
// Copyright (c) XU, Tianchen. All rights reserved.
//--------------------------------------------------------------------------------------

const char* SPSGaussianBlur =
R"(
#define GROUP_SIZE		8
#define RADIUS			16
#define SHARED_MEM_SIZE	(GROUP_SIZE + 2 * RADIUS)

#define DIV_UP(x, n)	(((x) + (n) - 1) / (n))

__constant sampler_t smpl = CLK_NORMALIZED_COORDS_FALSE | CLK_ADDRESS_CLAMP_TO_EDGE | CLK_FILTER_NEAREST;

float GaussianSigmaFromRadius(float r)
{
	return (r + 1.0f) / 3.0f;
}

float Gaussian(float r, float sigma)
{
	const float a = r / sigma;

	return exp(-0.5f * a * a);
}

kernel void clKernel(__write_only image2d_t result, __read_only image2d_t source)
{
	local float4 srcs[SHARED_MEM_SIZE][SHARED_MEM_SIZE];
	local float4 dsts[SHARED_MEM_SIZE][GROUP_SIZE];

	const float2 imageSize = (float2)(get_image_width(result), get_image_width(result));
	const int2 DTid = (int2)(get_global_id(0), get_global_id(1));
	const int2 Gid = (int2)(get_group_id(0), get_group_id(1));
	const int2 GTid = (int2)(get_local_id(0), get_local_id(1));

	// Load data into group-shared memory
	const uint n = DIV_UP(SHARED_MEM_SIZE, GROUP_SIZE);
	const int2 uvStart = GROUP_SIZE * Gid - RADIUS;
	for (int i = 0; i < n; ++i)
	{
		const int x = GROUP_SIZE * i + GTid.x;
		if (x < SHARED_MEM_SIZE)
		{
			for (int j = 0; j < n; ++j)
			{
				const int y = GROUP_SIZE * j + GTid.y;
				if (y < SHARED_MEM_SIZE)
				{
					const int2 uv = uvStart + (int2)(x, y);
					srcs[y][x] = read_imagef(source, smpl, uv);
				}
			}
		}
	}

	work_group_barrier(CLK_LOCAL_MEM_FENCE);

	const float sigma = GaussianSigmaFromRadius(RADIUS);
	const int x = GTid.x + RADIUS;

	// Horizontal filter
	for (uint k = 0; k < n; ++k)
	{
		const uint y = GROUP_SIZE * k + GTid.y;
		if (y >= SHARED_MEM_SIZE) break;

		float4 mu = 0.0f;
		float ws = 0.0f;
		for (int i = -RADIUS; i <= RADIUS; ++i)
		{
			const int xi = x + i;
			const float4 src = srcs[y][xi];

			const float w = Gaussian(i, sigma);
			mu += src * w;
			ws += w;
		}

		dsts[y][GTid.x] = mu / ws;
	}

	work_group_barrier(CLK_LOCAL_MEM_FENCE);

	// Vertical filter
	const int y = GTid.y + RADIUS;

	float4 mu = 0.0f;
	float ws = 0.0f;
	for (int i = -RADIUS; i <= RADIUS; ++i)
	{
		const int yi = y + i;
		const float4 src = dsts[yi][GTid.x];

		const float w = Gaussian(i, sigma);
		mu += src * w;
		ws += w;
	}

	write_imagef(result, DTid,  mu / ws);
}
)";
