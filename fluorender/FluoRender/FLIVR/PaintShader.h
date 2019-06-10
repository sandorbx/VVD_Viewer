//  
//  For more information, please see: http://software.sci.utah.edu
//  
//  The MIT License
//  
//  Copyright (c) 2004 Scientific Computing and Imaging Institute,
//  University of Utah.
//  
//  
//  Permission is hereby granted, free of charge, to any person obtaining a
//  copy of this software and associated documentation files (the "Software"),
//  to deal in the Software without restriction, including without limitation
//  the rights to use, copy, modify, merge, publish, distribute, sublicense,
//  and/or sell copies of the Software, and to permit persons to whom the
//  Software is furnished to do so, subject to the following conditions:
//  
//  The above copyright notice and this permission notice shall be included
//  in all copies or substantial portions of the Software.
//  
//  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
//  OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
//  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
//  THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
//  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
//  FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
//  DEALINGS IN THE SOFTWARE.
//  

#ifndef PaintShader_h
#define PaintShader_h

#include <string>
#include <vector>

#include "DLLExport.h"

#include "VulkanDevice.hpp"

namespace FLIVR
{
	#define PAINT_SAMPLER_NUM 1

	class ShaderProgram;

	class EXPORT_API PaintShader
	{
	public:
		PaintShader(VkDevice device);
		~PaintShader();

		bool create();

		inline VkDevice device() { return device_; }

		inline bool match(VkDevice device)
		{ 
			return (device_ == device);
		}

		inline ShaderProgram* program() { return program_; }

	protected:
		bool emit(std::string& s);

		VkDevice device_;

		ShaderProgram* program_;
	};

	class EXPORT_API PaintShaderFactory
	{
	public:
		PaintShaderFactory();
		PaintShaderFactory(std::vector<vks::VulkanDevice*> &devices);
		~PaintShaderFactory();

		ShaderProgram* shader(VkDevice device);

		void init(std::vector<vks::VulkanDevice*> &devices);

		struct PaintPipeline {
			VkDescriptorSetLayout descriptorSetLayout;
			VkPipelineLayout pipelineLayout;
			VkDescriptorSet descriptorSet;
		};

		void setupDescriptorSetLayout();
			
		std::map<vks::VulkanDevice*, PaintPipeline> pipeline_;

		std::vector<vks::VulkanDevice*> vdevices_;

	protected:
		std::vector<PaintShader*> shader_;
		int prev_shader_;
	};

} // end namespace FLIVR

#endif // PaintShader_h
