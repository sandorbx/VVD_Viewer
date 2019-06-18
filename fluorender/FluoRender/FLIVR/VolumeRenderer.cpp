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

#include <FLIVR/VolumeRenderer.h>
#include <FLIVR/VolShader.h>
#include <FLIVR/SegShader.h>
#include <FLIVR/VolCalShader.h>
#include <FLIVR/ShaderProgram.h>
#include <FLIVR/TextureBrick.h>
//#include <FLIVR/KernelProgram.h>
//#include <FLIVR/VolKernel.h>
#include "utility.h"
#include "../compatibility.h"
#include <fstream>
#include <iostream>
#include <glm/gtc/type_ptr.hpp>
#include <wx/wfstream.h>
#include <wx/txtstrm.h>
#include <wx/stdpaths.h>
//#include <boost/thread.hpp>
//#include <boost/bind.hpp>

#ifdef _WIN32
#include <windows.h>
#include <omp.h>
#endif

namespace FLIVR
{
#ifdef _WIN32
#undef min
#undef max
#endif

	double VolumeRenderer::sw_ = 0.0;
	
	std::vector<VolumeRenderer::VVolPipeline> VolumeRenderer::m_vol_pipelines;
	std::map<vks::VulkanDevice*, VkRenderPass> VolumeRenderer::m_vol_draw_pass;
	
	std::vector<VolumeRenderer::VSegPipeline> VolumeRenderer::m_seg_pipelines;
	std::map<vks::VulkanDevice*, VkRenderPass> VolumeRenderer::m_seg_draw_pass;
	
	std::vector<VolumeRenderer::VCalPipeline> VolumeRenderer::m_cal_pipelines;
	std::map<vks::VulkanDevice*, VkRenderPass> VolumeRenderer::m_cal_draw_pass;

	VolumeRenderer::VolumeRenderer(Texture* tex,
		const vector<Plane*> &planes,
		bool hiqual)
		:TextureRenderer(tex),
		buffer_scale_(1.0),
		//scalar scaling factor
		scalar_scale_(1.0),
		//gm range
		gm_scale_(1.0),
		//transfer function
		gamma3d_(1.0),
		gm_thresh_(0.0),
		offset_(1.0),
		lo_thresh_(0.0),
		hi_thresh_(1.0),
		color_(Color(1.0, 1.0, 1.0)),
		mask_color_(Color(0.0, 1.0, 0.0)),
		mask_color_set_(false),
		mask_thresh_(0.0),
		alpha_(1.0),
		//shading
		shading_(false),
		ambient_(1.0),
		diffuse_(1.0),
		specular_(1.0),
		shine_(10.0),
		//colormap mode
		colormap_mode_(0),
		colormap_low_value_(0.0),
		colormap_hi_value_(1.0),
		colormap_(0),
		colormap_proj_(0),
		//solid
		solid_(false),
		//interpolate
		interpolate_(true),
		//adaptive
		adaptive_(true),
		//clipping planes
		planes_(planes),
		//depth peel
		depth_peel_(0),
		//hi quality
		hiqual_(hiqual),
		//segmentation
		ml_mode_(0),
		mask_(false),
		label_(false),
		//scale factor
		noise_red_(false),
		sfactor_(1.0),
		filter_size_min_(0.0),
		filter_size_max_(0.0),
		filter_size_shp_(0.0),
		inv_(false),
		compression_(false),
		m_mask_hide_mode(VOL_MASK_HIDE_NONE),
		m_use_fog(false)
	{
		//mode
		mode_ = MODE_OVER;
		//done loop
		for (int i=0; i<TEXTURE_RENDER_MODES; i++)
			done_loop_[i] = false;

		m_clear_color = { { 0.0f, 0.0f, 0.0f, 0.0f } };
		
		if (m_vulkan)
		{
			m_vulkan->vol_shader_factory_->prepareUniformBuffers(m_volUniformBuffers);
			m_vulkan->seg_shader_factory_->prepareUniformBuffers(m_segUniformBuffers);
			for (auto dev : m_vulkan->devices)
				m_commandBuffers[dev] = dev->createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY);
			for (auto dev : m_vulkan->devices)
				m_seg_commandBuffers[dev] = dev->createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY);

			VkSemaphoreCreateInfo semaphoreInfo = vks::initializers::semaphoreCreateInfo();
			for (auto dev : m_vulkan->devices)
			{
				VkSemaphore tmp;
				VK_CHECK_RESULT(vkCreateSemaphore(dev->logicalDevice, &semaphoreInfo, nullptr, &tmp));
				m_volFinishedSemaphores[dev] = tmp;

				VkSemaphore tmp2;
				VK_CHECK_RESULT(vkCreateSemaphore(dev->logicalDevice, &semaphoreInfo, nullptr, &tmp2));
				m_filterFinishedSemaphores[dev] = tmp2;

				VkSemaphore tmp3;
				VK_CHECK_RESULT(vkCreateSemaphore(dev->logicalDevice, &semaphoreInfo, nullptr, &tmp3));
				m_renderFinishedSemaphores[dev] = tmp3;
			}
		}

		setupVertexDescriptions();

		m_prev_vol_pipeline = -1;
		m_prev_seg_pipeline = -1;
		m_prev_cal_pipeline = -1;
	}

	VolumeRenderer::VolumeRenderer(const VolumeRenderer& copy)
		:TextureRenderer(copy),
		buffer_scale_(copy.buffer_scale_),
		//scalar scale
		scalar_scale_(copy.scalar_scale_),
		//gm range
		gm_scale_(copy.gm_scale_),
		//transfer function
		gamma3d_(copy.gamma3d_),
		gm_thresh_(copy.gm_thresh_),
		offset_(copy.offset_),
		lo_thresh_(copy.lo_thresh_),
		hi_thresh_(copy.hi_thresh_),
		color_(copy.color_),
		mask_color_(copy.mask_color_),
		mask_color_set_(copy.mask_color_set_),
		mask_thresh_(0.0),
		alpha_(copy.alpha_),
		//shading
		shading_(copy.shading_),
		ambient_(copy.ambient_),
		diffuse_(copy.diffuse_),
		specular_(copy.specular_),
		shine_(copy.shine_),
		//colormap mode
		colormap_mode_(copy.colormap_mode_),
		colormap_low_value_(copy.colormap_low_value_),
		colormap_hi_value_(copy.colormap_hi_value_),
		colormap_(copy.colormap_),
		colormap_proj_(copy.colormap_proj_),
		//solid
		solid_(copy.solid_),
		//interpolate
		interpolate_(copy.interpolate_),
		//adaptive
		adaptive_(copy.adaptive_),
		//depth peel
		depth_peel_(copy.depth_peel_),
		//hi quality
		hiqual_(copy.hiqual_),
		//segmentation
		ml_mode_(copy.ml_mode_),
		mask_(copy.mask_),
		label_(copy.label_),
		//scale factor
		noise_red_(copy.noise_red_),
		sfactor_(1.0),
		filter_size_min_(0.0),
		filter_size_max_(0.0),
		filter_size_shp_(0.0),
		inv_(copy.inv_),
		compression_(copy.compression_),
		m_mask_hide_mode(copy.m_mask_hide_mode)
	{
		//mode
		mode_ = copy.mode_;
		//clipping planes
		for (int i=0; i<(int)copy.planes_.size(); i++)
		{
			Plane* plane = new Plane(*copy.planes_[i]);
			planes_.push_back(plane);
		}
		//done loop
		for (int i=0; i<TEXTURE_RENDER_MODES; i++)
			done_loop_[i] = false;

		m_clear_color = copy.m_clear_color;

		if (m_vulkan)
		{
			m_vulkan->vol_shader_factory_->prepareUniformBuffers(m_volUniformBuffers);
			m_vulkan->seg_shader_factory_->prepareUniformBuffers(m_segUniformBuffers);
			for (auto dev : m_vulkan->devices)
				m_commandBuffers[dev] = dev->createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY);
			for (auto dev : m_vulkan->devices)
				m_seg_commandBuffers[dev] = dev->createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY);

			VkSemaphoreCreateInfo semaphoreInfo = vks::initializers::semaphoreCreateInfo();
			for (auto dev : m_vulkan->devices)
			{
				VkSemaphore tmp;
				VK_CHECK_RESULT(vkCreateSemaphore(dev->logicalDevice, &semaphoreInfo, nullptr, &tmp));
				m_volFinishedSemaphores[dev] = tmp;

				VkSemaphore tmp2;
				VK_CHECK_RESULT(vkCreateSemaphore(dev->logicalDevice, &semaphoreInfo, nullptr, &tmp2));
				m_filterFinishedSemaphores[dev] = tmp2;

				VkSemaphore tmp3;
				VK_CHECK_RESULT(vkCreateSemaphore(dev->logicalDevice, &semaphoreInfo, nullptr, &tmp3));
				m_renderFinishedSemaphores[dev] = tmp3;
			}
		}

		setupVertexDescriptions();

		m_prev_vol_pipeline = copy.m_prev_vol_pipeline;
		m_prev_seg_pipeline = copy.m_prev_seg_pipeline;
		m_prev_cal_pipeline = copy.m_prev_cal_pipeline;
	}

	VolumeRenderer::~VolumeRenderer()
	{
		//release clipping planes
		for (int i=0; i<(int)planes_.size(); i++)
		{
			if (planes_[i])
				delete planes_[i];
		}
		planes_.clear();

		for (auto vdev : m_vulkan->devices)
		{
			m_volUniformBuffers[vdev].vert.destroy();
			m_volUniformBuffers[vdev].frag_base.destroy();
			m_segUniformBuffers[vdev].frag_base.destroy();
			vkFreeCommandBuffers(vdev->logicalDevice, vdev->commandPool, 1, &m_commandBuffers[vdev]);
			vkFreeCommandBuffers(vdev->logicalDevice, vdev->commandPool, 1, &m_seg_commandBuffers[vdev]);
			vkDestroySemaphore(vdev->logicalDevice, m_volFinishedSemaphores[vdev], nullptr);
			vkDestroySemaphore(vdev->logicalDevice, m_filterFinishedSemaphores[vdev], nullptr);
			vkDestroySemaphore(vdev->logicalDevice, m_renderFinishedSemaphores[vdev], nullptr);
		}
	}

	void VolumeRenderer::init()
	{
		if (m_vulkan)
		{
			for (auto dev : m_vulkan->devices)
			{
				m_vol_draw_pass[dev] = prepareRenderPass(dev, 1);
			}
			for (auto dev : m_vulkan->devices)
			{
				m_seg_draw_pass[dev] = prepareRenderPass(dev, 1);
			}
			for (auto dev : m_vulkan->devices)
			{
				m_cal_draw_pass[dev] = prepareRenderPass(dev, 1);
			}
		}
	}

	void VolumeRenderer::finalize()
	{
		if (m_vulkan)
		{
			for (auto dev : m_vulkan->devices)
			{
				vkDestroyRenderPass(dev->logicalDevice, m_vol_draw_pass[dev], nullptr);
			}
			for (auto dev : m_vulkan->devices)
			{
				vkDestroyRenderPass(dev->logicalDevice, m_seg_draw_pass[dev], nullptr);
			}
			for (auto dev : m_vulkan->devices)
			{
				vkDestroyRenderPass(dev->logicalDevice, m_cal_draw_pass[dev], nullptr);
			}

			for (auto &p : m_vol_pipelines)
			{
				vkDestroyPipeline(p.device->logicalDevice, p.vkpipeline, nullptr);
			}
			for (auto& p : m_seg_pipelines)
			{
				vkDestroyPipeline(p.device->logicalDevice, p.vkpipeline, nullptr);
			}
			for (auto& p : m_cal_pipelines)
			{
				vkDestroyPipeline(p.device->logicalDevice, p.vkpipeline, nullptr);
			}
		}
	}
	
	//render mode
	void VolumeRenderer::set_mode(RenderMode mode)
	{
		mode_ = mode;
	}

	//range and scale
	void VolumeRenderer::set_scalar_scale(double scale)
	{
		scalar_scale_ = scale;
	}

	double VolumeRenderer::get_scalar_scale()
	{
		return scalar_scale_;
	}

	void VolumeRenderer::set_gm_scale(double scale)
	{
		gm_scale_ = scale;
	}

	//transfer function properties
	void VolumeRenderer::set_gamma3d(double gamma)
	{
		gamma3d_ = gamma;
	}

	double VolumeRenderer::get_gamma3d()
	{
		return gamma3d_;
	}

	void VolumeRenderer::set_gm_thresh(double thresh)
	{
		gm_thresh_ = thresh;
	}

	double VolumeRenderer::get_gm_thresh()
	{
		return gm_thresh_;
	}

	void VolumeRenderer::set_offset(double offset)
	{
		offset_ = offset;
	}

	double VolumeRenderer::get_offset()
	{
		return offset_;
	}

	void VolumeRenderer::set_lo_thresh(double thresh)
	{
		lo_thresh_ = thresh;
	}

	double VolumeRenderer::get_lo_thresh()
	{
		return lo_thresh_;
	}

	void VolumeRenderer::set_hi_thresh(double thresh)
	{
		hi_thresh_ = thresh;
	}

	double VolumeRenderer::get_hi_thresh()
	{
		return hi_thresh_;
	}

	void VolumeRenderer::set_color(const Color color)
	{
		color_ = color;

		if (!mask_color_set_)
		{
			//generate opposite color for mask
			HSVColor hsv_color(color_);
			double h, s, v;
			if (hsv_color.sat() < 0.2)
				mask_color_ = Color(0.0, 1.0, 0.0);	//if low saturation, set to green
			else
			{
				double h0 = hsv_color.hue();
				h = h0<30.0?h0-180.0:h0<90.0?h0+120.0:h0<210.0?h0-120.0:h0-180.0;
				s = 1.0;
				v = 1.0;
				mask_color_ = Color(HSVColor(h<0.0?h+360.0:h, s, v));
			}
		}
	}

	Color VolumeRenderer::get_color()
	{
		return color_;
	}

	void VolumeRenderer::set_mask_color(const Color color, bool set)
	{
		mask_color_ = color;
		mask_color_set_ = set;
	}

	Color VolumeRenderer::get_mask_color()
	{
		return mask_color_;
	}

	void VolumeRenderer::set_mask_thresh(double thresh)
	{
		mask_thresh_ = thresh;
	}

	double VolumeRenderer::get_mask_thresh()
	{
		return mask_thresh_;
	}

	void VolumeRenderer::set_alpha(double alpha)
	{
		alpha_ = alpha;
	}

	double VolumeRenderer::get_alpha()
	{
		return alpha_;
	}

	//sampling rate
	void VolumeRenderer::set_sampling_rate(double rate)
	{
		sampling_rate_ = rate;
		//irate_ = rate>1.0 ? max(rate / 2.0, 1.0) : rate;
		irate_ = max(rate / 2.0, 0.01);
	}

	double VolumeRenderer::get_sampling_rate()
	{
		return sampling_rate_;
	}

	double VolumeRenderer::num_slices_to_rate(int num_slices)
	{
		const Vector diag = tex_->bbox()->diagonal();
		const Vector cell_diag(diag.x()/tex_->nx(),
			diag.y()/tex_->ny(),
			diag.z()/tex_->nz());
		const double dt = diag.length() / num_slices;
		const double rate = cell_diag.length() / dt;

		return rate;
	}

	int VolumeRenderer::get_slice_num()
	{
		return num_slices_;
	}

	//interactive modes
	void VolumeRenderer::set_interactive_rate(double rate)
	{
		irate_ = rate;
	}

	void VolumeRenderer::set_interactive_mode(bool mode)
	{
		imode_ = mode;
	}

	void VolumeRenderer::set_adaptive(bool b)
	{
		adaptive_ = b;
	}

	//clipping planes
	void VolumeRenderer::set_planes(vector<Plane*> *p)
	{
		int i;
		if (!planes_.empty())
		{
			for (i=0; i<(int)planes_.size(); i++)
			{
				if (planes_[i])
					delete planes_[i];
			}
			planes_.clear();
		}

		for (i=0; i<(int)p->size(); i++)
		{
			Plane *plane = new Plane(*(*p)[i]);
			planes_.push_back(plane);
		}
	}

	vector<Plane*>* VolumeRenderer::get_planes()
	{
		return &planes_;
	}
	
	//interpolation
	bool VolumeRenderer::get_interpolate() {return interpolate_; }

	void VolumeRenderer::set_interpolate(bool b) { interpolate_ = b;}

	//calculating scaling factor, etc
	//calculate scaling factor according to viewport and texture size
	double VolumeRenderer::CalcScaleFactor(double w, double h, double tex_w, double tex_h, double zoom)
	{
		double cs;
		double vs;
		double sf = 1.0;
		if (w > h)
		{
			cs = Clamp(tex_h, 500.0, 2000.0);
			vs = h;
		}
		else
		{
			cs = Clamp(tex_w, 500.0, 2000.0);
			vs = w;
		}
		double p1 = 1.282e9/(cs*cs*cs)+1.522;
		double p2 = -8.494e7/(cs*cs*cs)+0.496;
		sf = cs*p1/(vs*zoom)+p2;
		sf = Clamp(sf, 0.6, 1.0);
		return sf;
	}

	//calculate the filter sizes
	//calculate filter sizes according to viewport and texture size
	double VolumeRenderer::CalcFilterSize(int type,
		double w, double h, double tex_w, double tex_h,
		double zoom, double sf)
	{
		//clamped texture size
		double cs;
		//viewport size
		double vs;
		//filter size
		double size = 0.0;
		if (w > h)
		{
			cs = Clamp(tex_h, 500.0, 1200.0);
			vs = h;
		}
		else
		{
			cs = Clamp(tex_w, 500.0, 1200.0);
			vs = w;
		}

		switch (type)
		{
		case 1:	//min filter
			{
				double p = 0.29633+(-2.18448e-4)*cs;
				size = (p*zoom+0.24512)*sf;
				size = Clamp(size, 0.0, 2.0);
			}
			break;
		case 2:	//max filter
			{
				double p1 = 0.26051+(-1.90542e-4)*cs;
				double p2 = -0.29188+(2.45276e-4)*cs;
				p2 = min(p2, 0.0);
				size = (p1*zoom+p2)*sf;
				size = Clamp(size, 0.0, 2.0);
			}
			break;
		case 3:	//sharpening filter
			{
				//double p = 0.012221;
				//size = p*zoom;
				//size = Clamp(size, 0.0, 0.25);
				double sf11 = sqrt(tex_w*tex_w + tex_h*tex_h)/vs;
				size = zoom / sf11 / 10.0;
				size = size<1.0?0.0:size;
				size = Clamp(size, 0.0, 0.3);
			}
			break;
		case 4:	//blur filter
			{
				double sf11 = sqrt(tex_w*tex_w + tex_h*tex_h)/vs;
				size = zoom / sf11 / 2.0;
				size = size<1.0?0.5:size;
				size = Clamp(size, 0.1, 1.0);
			}
		}

		//adjusting for screen size
		//double af = vs/800.0;

		return size;
	}

	double VolumeRenderer::compute_dt_fac(double sampling_frq_fac, double *rate_fac)
	{
		double maxlen;
		double vdmaxlen;
		Transform *field_trans = tex_->transform();

		double mvmat[16] =
			{m_mv_mat[0][0], m_mv_mat[0][1], m_mv_mat[0][2], m_mv_mat[0][3],
			 m_mv_mat[1][0], m_mv_mat[1][1], m_mv_mat[1][2], m_mv_mat[1][3],
			 m_mv_mat[2][0], m_mv_mat[2][1], m_mv_mat[2][2], m_mv_mat[2][3],
			 m_mv_mat[3][0], m_mv_mat[3][1], m_mv_mat[3][2], m_mv_mat[3][3]};

		Vector spcv[3] = {Vector(1.0, 0.0, 0.0), Vector(0.0, 1.0, 0.0), Vector(0.0, 0.0, 1.0)};
		vdmaxlen = -1.0;

		for(int i = 0; i < 3 ; i++)
		{
			// index space view direction
			Vector v;
			v = field_trans->project(spcv[i]);
			v.safe_normalize();
			v = field_trans->project(spcv[i]);

			double len = Dot(spcv[i], v);
			if(len > vdmaxlen) vdmaxlen = len;

		}

		if(sampling_frq_fac > 0.0)maxlen = sampling_frq_fac;
		else maxlen = vdmaxlen;

		if(rate_fac != nullptr && sampling_frq_fac > 0.0) *rate_fac = sampling_frq_fac / vdmaxlen;

		// index space view direction
		Vector mv_ray = Vector(-mvmat[2], -mvmat[6], -mvmat[10]);//normalized
		Vector v = field_trans->project(Vector(-mvmat[2], -mvmat[6], -mvmat[10]));
		v.safe_normalize();
		v = field_trans->project(v);

		double l = Dot(mv_ray, v);
		return maxlen / l;
	}

	//sclfac: m_proj_mat[0][0] (ortho)
	double VolumeRenderer::compute_dt_fac_1px(uint32_t w, uint32_t h, double sclfac)
	{
		if (sclfac <= 0.0)
			return 1.0;

		double maxlen;
		double vdmaxlen;
		Transform *field_trans = tex_->transform();

		double mvmat[16] =
			{m_mv_mat[0][0], m_mv_mat[0][1], m_mv_mat[0][2], m_mv_mat[0][3],
			 m_mv_mat[1][0], m_mv_mat[1][1], m_mv_mat[1][2], m_mv_mat[1][3],
			 m_mv_mat[2][0], m_mv_mat[2][1], m_mv_mat[2][2], m_mv_mat[2][3],
			 m_mv_mat[3][0], m_mv_mat[3][1], m_mv_mat[3][2], m_mv_mat[3][3]};

		uint32_t mindim = min(w, h);

		double pxlen = 1.0 / mindim / sclfac;
		
		// index space view direction
		Vector mv_ray = Vector(-mvmat[2], -mvmat[6], -mvmat[10]);//normalized
		Vector v = field_trans->project(Vector(-mvmat[2], -mvmat[6], -mvmat[10])); //for scaling (unproject normal)
		/*
		double f_e_len = v.length();
		v = field_trans->project(v);
		double dt = (pxlen * f_e_len) / Dot(mv_ray, v);
		*/

		Vector p = field_trans->unproject(pxlen*mv_ray);
		v.safe_normalize();
		double dt = Dot(p, v);

		return dt;
	}

	bool VolumeRenderer::test_against_view_clip(const BBox &bbox, const BBox &tbox, const BBox &dbox, bool persp)
	{
		if (!test_against_view(bbox, persp))
			return false;

		Transform *tform = tex_->transform();
		if (!tform)
			return true;
		
		Vector b_u[3];
		b_u[0] = Vector(1.0, 0.0, 0.0);
		b_u[1] = Vector(0.0, 1.0, 0.0);
		b_u[2] = Vector(0.0, 0.0, 1.0);
		
		Point bmax = bbox.max();
		Point bmin = bbox.min();

		bmax = tform->transform(bmax);
		bmin = tform->transform(bmin);

		float b_e[3];
		b_e[0] = abs(bmax.x() - bmin.x()) * 0.5f;
		b_e[1] = abs(bmax.y() - bmin.y()) * 0.5f;
		b_e[2] = abs(bmax.z() - bmin.z()) * 0.5f;

		Vector b_c = (bmax + bmin) * 0.5f;
		
		Plane* px1 = planes_[0];
		Plane* px2 = planes_[1];
		Plane* py1 = planes_[2];
		Plane* py2 = planes_[3];
		Plane* pz1 = planes_[4];
		Plane* pz2 = planes_[5];

		//calculate 4 lines
		Vector lv_x1z1, lv_x1z2, lv_x2z1, lv_x2z2;
		Point lp_x1z1, lp_x1z2, lp_x2z1, lp_x2z2;
		//x1z1
		if (!px1->Intersect(*pz1, lp_x1z1, lv_x1z1))
			return true;
		//x1z2
		if (!px1->Intersect(*pz2, lp_x1z2, lv_x1z2))
			return true;
		//x2z1
		if (!px2->Intersect(*pz1, lp_x2z1, lv_x2z1))
			return true;
		//x2z2
		if (!px2->Intersect(*pz2, lp_x2z2, lv_x2z2))
			return true;

		//calculate 8 points
		Point pp[8];
		//p0 = l_x1z1 * py1
		if (!py1->Intersect(lp_x1z1, lv_x1z1, pp[0]))
			return true;
		//p1 = l_x1z2 * py1
		if (!py1->Intersect(lp_x1z2, lv_x1z2, pp[1]))
			return true;
		//p2 = l_x2z1 *py1
		if (!py1->Intersect(lp_x2z1, lv_x2z1, pp[2]))
			return true;
		//p3 = l_x2z2 * py1
		if (!py1->Intersect(lp_x2z2, lv_x2z2, pp[3]))
			return true;
		//p4 = l_x1z1 * py2
		if (!py2->Intersect(lp_x1z1, lv_x1z1, pp[4]))
			return true;
		//p5 = l_x1z2 * py2
		if (!py2->Intersect(lp_x1z2, lv_x1z2, pp[5]))
			return true;
		//p6 = l_x2z1 * py2
		if (!py2->Intersect(lp_x2z1, lv_x2z1, pp[6]))
			return true;
		//p7 = l_x2z2 * py2
		if (!py2->Intersect(lp_x2z2, lv_x2z2, pp[7]))
			return true;

		for (int i = 0; i < 8; i++)
			pp[i] = tform->transform(pp[i]);
		
		Vector a_u[3];
		Vector scv(tform->get_mat_val(0,0), tform->get_mat_val(1,1), tform->get_mat_val(2,2));
		Vector n;
		n = px1->normal();
		a_u[0] = Vector(n.x()/scv.x(), n.y()/scv.y(), n.z()/scv.z());
		a_u[0].safe_normalize();
		n = py1->normal();
		a_u[1] = Vector(n.x()/scv.x(), n.y()/scv.y(), n.z()/scv.z());
		a_u[1].safe_normalize();
		n = pz1->normal();
		a_u[2] = Vector(n.x()/scv.x(), n.y()/scv.y(), n.z()/scv.z());
		a_u[2].safe_normalize();

		Vector xx = pp[2] - pp[0];
		Vector yy = pp[4] - pp[0];
		Vector zz = pp[1] - pp[0];
		
		float a_e[3];
		a_e[0] = xx.length() * 0.5f;
		a_e[1] = yy.length() * 0.5f;
		a_e[2] = zz.length() * 0.5f;

		Vector a_c = Vector(pp[0].x(),pp[0].y(),pp[0].z()) + (xx + yy + zz) * 0.5f;
		
		const float EPSILON = 1.175494e-37;

		float R[3][3], AbsR[3][3];
		for(int i = 0; i < 3; i++)
		{
			for(int j = 0; j < 3; j++)
			{
				R[i][j] = Dot(a_u[i], b_u[j]);
				AbsR[i][j] = fabsf(R[i][j]) + EPSILON;
			}
		}

		Vector t = b_c - a_c;
		t = Vector(Dot(t, a_u[0]), Dot(t, a_u[1]), Dot(t, a_u[2]));

		//L=A0, L=A1, L=A2
		float ra, rb;

		for(int i = 0; i < 3; i++)
		{
			ra = a_e[i];
			rb = b_e[0] * AbsR[i][0] + b_e[1] * AbsR[i][1] + b_e[2] * AbsR[i][2];
			if(fabsf(t[i]) > ra + rb)
				return false;
		}
		//L=B0, L=B1, L=B2
		for(int i = 0; i < 3; i++)
		{
			ra = a_e[0] * AbsR[0][i] + a_e[1] * AbsR[1][i] + a_e[2] * AbsR[2][i];
			rb = b_e[i];
			if(fabsf(t[0] * R[0][i] + t[1] * R[1][i] + t[2] * R[2][i]) > ra + rb)
				return false;
		}

		//L=A0 X B0
		ra = a_e[1] * AbsR[2][0] + a_e[2] * AbsR[1][0];
		rb = b_e[1] * AbsR[0][2] + b_e[2] * AbsR[0][1];
		if(fabsf(t[2] * R[1][0] - t[1] * R[2][0]) > ra + rb)
			return false;

		//L=A0 X B1
		ra = a_e[1] * AbsR[2][1] + a_e[2] * AbsR[1][1];
		rb = b_e[0] * AbsR[0][2] + b_e[2] * AbsR[0][0];
		if(fabsf(t[2] * R[1][1] - t[1] * R[2][1]) > ra + rb)
			return false;

		//L=A0 X B2
		ra = a_e[1] * AbsR[2][2] + a_e[2] * AbsR[1][2];
		rb = b_e[0] * AbsR[0][1] + b_e[1] * AbsR[0][0];
		if(fabsf(t[2] * R[1][2] - t[1] * R[2][2]) > ra + rb)
			return false;

		//L=A1 X B0
		ra = a_e[0] * AbsR[2][0] + a_e[2] * AbsR[0][0];
		rb = b_e[1] * AbsR[1][2] + b_e[2] * AbsR[1][1];
		if(fabsf(t[0] * R[2][0] - t[2] * R[0][0]) > ra + rb)
			return false;

		//L=A1 X B1
		ra = a_e[0] * AbsR[2][1] + a_e[2] * AbsR[0][1];
		rb = b_e[0] * AbsR[1][2] + b_e[2] * AbsR[1][0];
		if(fabsf(t[0] * R[2][1] - t[2] * R[0][1]) > ra + rb)
			return false;

		//L=A1 X B2
		ra = a_e[0] * AbsR[2][2] + a_e[2] * AbsR[0][2];
		rb = b_e[0] * AbsR[1][1] + b_e[1] * AbsR[1][0];
		if(fabsf(t[0] * R[2][2] - t[2] * R[0][2]) > ra + rb)
			return false;

		//L=A2 X B0
		ra = a_e[0] * AbsR[1][0] + a_e[1] * AbsR[0][0];
		rb = b_e[1] * AbsR[2][2] + b_e[2] * AbsR[2][1];
		if(fabsf(t[1] * R[0][0] - t[0] * R[1][0]) > ra + rb)
			return false;

		//L=A2 X B1
		ra = a_e[0] * AbsR[1][1] + a_e[1] * AbsR[0][1];
		rb = b_e[0] * AbsR[2][2] + b_e[2] * AbsR[2][0];
		if(fabsf(t[1] * R[0][1] - t[0] * R[1][1]) > ra + rb)
			return false;

		//L=A2 X B2
		ra = a_e[0] * AbsR[1][2] + a_e[1] * AbsR[0][2];
		rb = b_e[0] * AbsR[2][1] + b_e[1] * AbsR[2][0];
		if(fabsf(t[1] * R[0][2] - t[0] * R[1][2]) > ra + rb)
			return false;

		return true;
	}

	void VolumeRenderer::eval_ml_mode()
	{
		//reassess the mask/label mode
		//0-normal, 1-render with mask, 2-render with mask excluded
		//3-random color with label, 4-random color with label+mask
		switch (ml_mode_)
		{
		case 0:
			mask_ = false;
			label_ = false;
			break;
		case 1:
			if (tex_->nmask() == -1)
			{
				mask_ = false;
				ml_mode_ = 0;
			}
			else
				mask_ = true;
			label_ = false;
			break;
		case 2:
			if (tex_->nmask() == -1)
			{
				mask_ = false;
				ml_mode_ = 0;
			}
			else
				mask_ = true;
			label_ = false;
			break;
		case 3:
			if (tex_->nlabel() == -1)
			{
				label_ = false;
				ml_mode_ = 0;
			}
			else
				label_ = true;
			mask_ = false;
			break;
		case 4:
			if (tex_->nlabel() == -1)
			{
				if (tex_->nmask()>-1)
				{
					mask_ = true;
					label_ = false;
					ml_mode_ = 1;
				}
				else
				{
					mask_ = label_ = false;
					ml_mode_ = 0;
				}
			}
			else
				mask_ = label_ = true;
			break;
		}
	}

	void VolumeRenderer::setupVertexDescriptions()
	{
		// Binding description
		m_vertices.inputBinding.resize(1);
		m_vertices.inputBinding[0] =
			vks::initializers::vertexInputBindingDescription(
				0,
				sizeof(Vertex),
				VK_VERTEX_INPUT_RATE_VERTEX);

		// Attribute descriptions
		// Describes memory layout and shader positions
		m_vertices.inputAttributes.resize(2);
		// Location 0 : Position
		m_vertices.inputAttributes[0] =
			vks::initializers::vertexInputAttributeDescription(
				0,
				0,
				VK_FORMAT_R32G32B32_SFLOAT,
				offsetof(Vertex, pos));
		// Location 1 : Texture coordinates
		m_vertices.inputAttributes[1] =
			vks::initializers::vertexInputAttributeDescription(
				0,
				1,
				VK_FORMAT_R32G32B32_SFLOAT,
				offsetof(Vertex, uv));

		m_vertices.inputState = vks::initializers::pipelineVertexInputStateCreateInfo();
		m_vertices.inputState.vertexBindingDescriptionCount = static_cast<uint32_t>(m_vertices.inputBinding.size());
		m_vertices.inputState.pVertexBindingDescriptions = m_vertices.inputBinding.data();
		m_vertices.inputState.vertexAttributeDescriptionCount = static_cast<uint32_t>(m_vertices.inputAttributes.size());
		m_vertices.inputState.pVertexAttributeDescriptions = m_vertices.inputAttributes.data();
	}

	VkRenderPass VolumeRenderer::prepareRenderPass(vks::VulkanDevice* device, int attatchment_num)
	{
		VkRenderPass ret = VK_NULL_HANDLE;
		
		// Create a separate render pass for the offscreen rendering as it may differ from the one used for scene rendering
		std::vector<VkAttachmentDescription> attchmentDescriptions;
		VkAttachmentDescription ad = {};
		// Color attachment
		ad.format = VK_FORMAT_R32G32B32A32_SFLOAT;
		ad.samples = VK_SAMPLE_COUNT_1_BIT;
		ad.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		ad.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		ad.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		ad.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		ad.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		ad.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

		for (uint32_t i = 0; i < attatchment_num; i++)
			attchmentDescriptions.push_back(ad);

		std::vector<VkAttachmentReference> colorReferences;
		for (uint32_t i = 0; i < attatchment_num; i++)
		{
			VkAttachmentReference cr = { i, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
			colorReferences.push_back(cr);
		}

		VkSubpassDescription subpassDescription = {};
		subpassDescription.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
		subpassDescription.colorAttachmentCount = static_cast<uint32_t>(colorReferences.size());
		subpassDescription.pColorAttachments = colorReferences.data();

		// Use subpass dependencies for layout transitions
		std::array<VkSubpassDependency, 2> dependencies;

		dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
		dependencies[0].dstSubpass = 0;
		dependencies[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
		dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		dependencies[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
		dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		dependencies[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

		dependencies[1].srcSubpass = 0;
		dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
		dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		dependencies[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
		dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		dependencies[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
		dependencies[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

		// Create the actual renderpass
		VkRenderPassCreateInfo renderPassInfo = {};
		renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
		renderPassInfo.attachmentCount = static_cast<uint32_t>(attchmentDescriptions.size());
		renderPassInfo.pAttachments = attchmentDescriptions.data();
		renderPassInfo.subpassCount = 1;
		renderPassInfo.pSubpasses = &subpassDescription;
		renderPassInfo.dependencyCount = static_cast<uint32_t>(dependencies.size());
		renderPassInfo.pDependencies = dependencies.data();

		VK_CHECK_RESULT(vkCreateRenderPass(device->logicalDevice, &renderPassInfo, nullptr, &ret));

		return ret;
	}

	VolumeRenderer::VVolPipeline VolumeRenderer::prepareVolPipeline(vks::VulkanDevice* device, int mode, int update_order, int colormap_mode)
	{
		VVolPipeline ret_pipeline;

		bool use_fog = m_use_fog && colormap_mode_ != 2;
		ShaderProgram *shader = m_vulkan->vol_shader_factory_->shader(
			device->logicalDevice,
			false, tex_->nc(),
			shading_, use_fog,
			depth_peel_, true,
			hiqual_, ml_mode_,
			colormap_mode_, colormap_, colormap_proj_,
			solid_, 1, tex_->nmask() != -1 ? m_mask_hide_mode : VOL_MASK_HIDE_NONE);

		if (m_prev_vol_pipeline >= 0) {
			if (m_vol_pipelines[m_prev_vol_pipeline].device == device &&
				m_vol_pipelines[m_prev_vol_pipeline].shader == shader &&
				m_vol_pipelines[m_prev_vol_pipeline].mode == mode &&
				m_vol_pipelines[m_prev_vol_pipeline].update_order == update_order &&
				m_vol_pipelines[m_prev_vol_pipeline].colormap_mode == colormap_mode)
				return m_vol_pipelines[m_prev_vol_pipeline];
		}
		for (int i = 0; i < m_vol_pipelines.size(); i++) {
			if (m_vol_pipelines[i].device == device &&
				m_vol_pipelines[i].shader == shader &&
				m_vol_pipelines[i].mode == mode &&
				m_vol_pipelines[i].update_order == update_order &&
				m_vol_pipelines[i].colormap_mode == colormap_mode)
			{
				m_prev_vol_pipeline = i;
				return m_vol_pipelines[i];
			}
		}

		VkPipelineInputAssemblyStateCreateInfo inputAssemblyState =
			vks::initializers::pipelineInputAssemblyStateCreateInfo(
				VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
				0,
				VK_FALSE);

		VkPipelineRasterizationStateCreateInfo rasterizationState =
			vks::initializers::pipelineRasterizationStateCreateInfo(
				VK_POLYGON_MODE_FILL,
				VK_CULL_MODE_NONE,
				VK_FRONT_FACE_COUNTER_CLOCKWISE,
				0);

		VkPipelineDepthStencilStateCreateInfo depthStencilState =
			vks::initializers::pipelineDepthStencilStateCreateInfo(
				VK_FALSE,
				VK_FALSE,
				VK_COMPARE_OP_NEVER);

		VkPipelineViewportStateCreateInfo viewportState =
			vks::initializers::pipelineViewportStateCreateInfo(1, 1, 0);

		VkPipelineMultisampleStateCreateInfo multisampleState =
			vks::initializers::pipelineMultisampleStateCreateInfo(
				VK_SAMPLE_COUNT_1_BIT,
				0);

		std::vector<VkDynamicState> dynamicStateEnables = {
			VK_DYNAMIC_STATE_VIEWPORT,
			VK_DYNAMIC_STATE_SCISSOR
		};
		VkPipelineDynamicStateCreateInfo dynamicState =
			vks::initializers::pipelineDynamicStateCreateInfo(
				dynamicStateEnables.data(),
				static_cast<uint32_t>(dynamicStateEnables.size()),
				0);

		VolShaderFactory::VolPipelineSettings pipeline_settings = m_vulkan->vol_shader_factory_->pipeline_[device];
		VkGraphicsPipelineCreateInfo pipelineCreateInfo =
			vks::initializers::pipelineCreateInfo(
				pipeline_settings.pipelineLayout,
				m_vol_draw_pass[device],
				0);

		pipelineCreateInfo.pVertexInputState = &m_vertices.inputState;
		pipelineCreateInfo.pInputAssemblyState = &inputAssemblyState;
		pipelineCreateInfo.pRasterizationState = &rasterizationState;
		pipelineCreateInfo.pMultisampleState = &multisampleState;
		pipelineCreateInfo.pViewportState = &viewportState;
		pipelineCreateInfo.pDepthStencilState = &depthStencilState;
		pipelineCreateInfo.pDynamicState = &dynamicState;
		pipelineCreateInfo.stageCount = 2;

		//blend mode
		VkBool32 enable_blend;
		VkBlendOp blendop;
		VkBlendFactor src_blend, dst_blend;
		
		enable_blend = VK_TRUE;
		switch (mode_)
		{
		case MODE_MIP:
			blendop = VK_BLEND_OP_MAX;
			src_blend = VK_BLEND_FACTOR_ONE;
			dst_blend = VK_BLEND_FACTOR_ONE;
			break;
		case MODE_OVER:
		default:
			blendop = VK_BLEND_OP_ADD;
			if (update_order_ == 0)
			{
				src_blend = VK_BLEND_FACTOR_ONE;
				dst_blend = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
			}
			else if (update_order_ == 1)
			{
				src_blend = VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
				dst_blend = VK_BLEND_FACTOR_ONE;
			}
			break;
		}
		enable_blend = (colormap_mode_ == 3) ? VK_FALSE : VK_TRUE;

		VkPipelineColorBlendAttachmentState blendAttachmentState =
			vks::initializers::pipelineColorBlendAttachmentState(
				enable_blend,
				blendop,
				src_blend,
				dst_blend,
				0xf);

		VkPipelineColorBlendStateCreateInfo colorBlendState =
			vks::initializers::pipelineColorBlendStateCreateInfo(
				1,
				&blendAttachmentState);
		pipelineCreateInfo.pColorBlendState = &colorBlendState;

		// Load shaders
		std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages;
		shaderStages[0] = shader->get_vertex_shader();
		shaderStages[1] = shader->get_fragment_shader();
		pipelineCreateInfo.pStages = shaderStages.data();

		VK_CHECK_RESULT(
			vkCreateGraphicsPipelines(
				device->logicalDevice,
				m_vulkan->getPipelineCache(),
				1,
				&pipelineCreateInfo,
				nullptr,
				&ret_pipeline.vkpipeline
			)
		);

		ret_pipeline.device = device;
		ret_pipeline.renderpass = m_vol_draw_pass[device];
		ret_pipeline.shader = shader;
		ret_pipeline.mode = mode;
		ret_pipeline.update_order = update_order;
		ret_pipeline.colormap_mode = colormap_mode;

		m_vol_pipelines.push_back(ret_pipeline);
		m_prev_vol_pipeline = m_vol_pipelines.size() - 1;

		return ret_pipeline;
	}

	void VolumeRenderer::saveScreenshot(const char* filename)
	{
		VkPhysicalDevice physicalDevice = m_vulkan->vulkanDevice->physicalDevice;
		VkDevice device = m_vulkan->vulkanDevice->logicalDevice;

		bool supportsBlit = true;

		// Check blit support for source and destination
		VkFormatProperties formatProps;

		// Check if the device supports blitting from optimal images (the swapchain images are in optimal format)
		vkGetPhysicalDeviceFormatProperties(physicalDevice, blend_tex_id_->format, &formatProps);
		if (!(formatProps.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_SRC_BIT)) {
			std::cerr << "Device does not support blitting from optimal tiled images, using copy instead of blit!" << std::endl;
			supportsBlit = false;
		}

		// Check if the device supports blitting to linear images 
		vkGetPhysicalDeviceFormatProperties(physicalDevice, VK_FORMAT_R8G8B8A8_UNORM, &formatProps);
		if (!(formatProps.linearTilingFeatures & VK_FORMAT_FEATURE_BLIT_DST_BIT)) {
			std::cerr << "Device does not support blitting to linear tiled images, using copy instead of blit!" << std::endl;
			supportsBlit = false;
		}

		// Source for the copy is the last rendered swapchain image
		VkImage srcImage = blend_tex_id_->image;

		// Create the linear tiled destination image to copy to and to read the memory from
		VkImageCreateInfo imageCreateCI(vks::initializers::imageCreateInfo());
		imageCreateCI.imageType = VK_IMAGE_TYPE_2D;
		// Note that vkCmdBlitImage (if supported) will also do format conversions if the swapchain color format would differ
		imageCreateCI.format = VK_FORMAT_R8G8B8A8_UNORM;
		imageCreateCI.extent.width = blend_tex_id_->w;
		imageCreateCI.extent.height = blend_tex_id_->h;
		imageCreateCI.extent.depth = 1;
		imageCreateCI.arrayLayers = 1;
		imageCreateCI.mipLevels = 1;
		imageCreateCI.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		imageCreateCI.samples = VK_SAMPLE_COUNT_1_BIT;
		imageCreateCI.tiling = VK_IMAGE_TILING_LINEAR;
		imageCreateCI.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT;
		// Create the image
		VkImage dstImage;
		VK_CHECK_RESULT(vkCreateImage(device, &imageCreateCI, nullptr, &dstImage));
		// Create memory to back up the image
		VkMemoryRequirements memRequirements;
		VkMemoryAllocateInfo memAllocInfo(vks::initializers::memoryAllocateInfo());
		VkDeviceMemory dstImageMemory;
		vkGetImageMemoryRequirements(device, dstImage, &memRequirements);
		memAllocInfo.allocationSize = memRequirements.size;
		// Memory must be host visible to copy from
		memAllocInfo.memoryTypeIndex = m_vulkan->vulkanDevice->getMemoryType(memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
		VK_CHECK_RESULT(vkAllocateMemory(device, &memAllocInfo, nullptr, &dstImageMemory));
		VK_CHECK_RESULT(vkBindImageMemory(device, dstImage, dstImageMemory, 0));

		// Do the actual blit from the swapchain image to our host visible destination image
		VkCommandBuffer copyCmd = m_vulkan->vulkanDevice->createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);

		// Transition destination image to transfer destination layout
		vks::tools::insertImageMemoryBarrier(
			copyCmd,
			dstImage,
			0,
			VK_ACCESS_TRANSFER_WRITE_BIT,
			VK_IMAGE_LAYOUT_UNDEFINED,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			VkImageSubresourceRange{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 });

		// Transition swapchain image from present to transfer source layout
		vks::tools::insertImageMemoryBarrier(
			copyCmd,
			srcImage,
			VK_ACCESS_MEMORY_READ_BIT,
			VK_ACCESS_TRANSFER_READ_BIT,
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			VkImageSubresourceRange{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 });

		// If source and destination support blit we'll blit as this also does automatic format conversion (e.g. from BGR to RGB)
		if (supportsBlit)
		{
			// Define the region to blit (we will blit the whole swapchain image)
			VkOffset3D blitSize;
			blitSize.x = blend_tex_id_->w;
			blitSize.y = blend_tex_id_->h;
			blitSize.z = 1;
			VkImageBlit imageBlitRegion{};
			imageBlitRegion.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			imageBlitRegion.srcSubresource.layerCount = 1;
			imageBlitRegion.srcOffsets[1] = blitSize;
			imageBlitRegion.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			imageBlitRegion.dstSubresource.layerCount = 1;
			imageBlitRegion.dstOffsets[1] = blitSize;

			// Issue the blit command
			vkCmdBlitImage(
				copyCmd,
				srcImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
				dstImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				1,
				&imageBlitRegion,
				VK_FILTER_NEAREST);
		}
		else
		{
			// Otherwise use image copy (requires us to manually flip components)
			VkImageCopy imageCopyRegion{};
			imageCopyRegion.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			imageCopyRegion.srcSubresource.layerCount = 1;
			imageCopyRegion.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			imageCopyRegion.dstSubresource.layerCount = 1;
			imageCopyRegion.extent.width = blend_tex_id_->w;
			imageCopyRegion.extent.height = blend_tex_id_->h;
			imageCopyRegion.extent.depth = 1;

			// Issue the copy command
			vkCmdCopyImage(
				copyCmd,
				srcImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
				dstImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				1,
				&imageCopyRegion);
		}

		// Transition destination image to general layout, which is the required layout for mapping the image memory later on
		vks::tools::insertImageMemoryBarrier(
			copyCmd,
			dstImage,
			VK_ACCESS_TRANSFER_WRITE_BIT,
			VK_ACCESS_MEMORY_READ_BIT,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			VK_IMAGE_LAYOUT_GENERAL,
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			VkImageSubresourceRange{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 });

		// Transition back the swap chain image after the blit is done
		vks::tools::insertImageMemoryBarrier(
			copyCmd,
			srcImage,
			VK_ACCESS_TRANSFER_READ_BIT,
			VK_ACCESS_MEMORY_READ_BIT,
			VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			VkImageSubresourceRange{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 });

		m_vulkan->vulkanDevice->flushCommandBuffer(copyCmd, m_vulkan->vulkanDevice->queue);

		// Get layout of the image (including row pitch)
		VkImageSubresource subResource{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 0 };
		VkSubresourceLayout subResourceLayout;
		vkGetImageSubresourceLayout(device, dstImage, &subResource, &subResourceLayout);

		// Map image memory so we can start copying from it
		const char* data;
		vkMapMemory(device, dstImageMemory, 0, VK_WHOLE_SIZE, 0, (void**)& data);
		data += subResourceLayout.offset;

		std::ofstream file(filename, std::ios::out | std::ios::binary);

		// ppm header
		file << "P6\n" << blend_tex_id_->w << "\n" << blend_tex_id_->h << "\n" << 255 << "\n";

		// If source is BGR (destination is always RGB) and we can't use blit (which does automatic conversion), we'll have to manually swizzle color components
		bool colorSwizzle = false;
		// Check if source is BGR 
		// Note: Not complete, only contains most common and basic BGR surface formats for demonstation purposes
		if (!supportsBlit)
		{
			std::vector<VkFormat> formatsBGR = { VK_FORMAT_B8G8R8A8_SRGB, VK_FORMAT_B8G8R8A8_UNORM, VK_FORMAT_B8G8R8A8_SNORM };
			colorSwizzle = (std::find(formatsBGR.begin(), formatsBGR.end(), blend_tex_id_->format) != formatsBGR.end());
		}

		// ppm binary pixel data
		for (uint32_t y = 0; y < blend_tex_id_->h; y++)
		{
			unsigned int* row = (unsigned int*)data;
			for (uint32_t x = 0; x < blend_tex_id_->w; x++)
			{
				if (colorSwizzle)
				{
					file.write((char*)row + 2, 1);
					file.write((char*)row + 1, 1);
					file.write((char*)row, 1);
				}
				else
				{
					file.write((char*)row, 3);
				}
				row++;
			}
			data += subResourceLayout.rowPitch;
		}
		file.close();

		std::cout << "Screenshot saved to disk" << std::endl;

		// Clean up resources
		vkUnmapMemory(device, dstImageMemory);
		vkFreeMemory(device, dstImageMemory, nullptr);
		vkDestroyImage(device, dstImage, nullptr);
	}

	void VolumeRenderer::prepareVertexBuffers(vks::VulkanDevice* device, double dt)
	{
		if (!tex_ || !m_vulkan)
			return;

		Vector diag = tex_->GetBrickIdSpaceMaxExtent();

		VkDeviceSize num_slices = (VkDeviceSize)(diag.length() / dt) + 2;
		VkDeviceSize brick_num = tex_->get_brick_num();

		VkDeviceSize vertbufsize = num_slices * 6 * 6 * sizeof(float) * brick_num;
		VkDeviceSize idxbufsize = num_slices * 4 * 3 * sizeof(unsigned int) * brick_num;

		if (m_vertbufs[device].vertexBuffer.buffer && m_vertbufs[device].vertexBuffer.size < vertbufsize)
			m_vertbufs[device].vertexBuffer.destroy();
		if (m_vertbufs[device].indexBuffer.buffer && m_vertbufs[device].indexBuffer.size < idxbufsize)
			m_vertbufs[device].indexBuffer.destroy();

		if (!m_vertbufs[device].vertexBuffer.buffer)
		{
			device->createBuffer(VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
								 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
								 &m_vertbufs[device].vertexBuffer,
								 vertbufsize);
			m_vertbufs[device].vertexBuffer.map();
		}

		if (!m_vertbufs[device].indexBuffer.buffer)
		{
			device->createBuffer(VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
								 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
								 &m_vertbufs[device].indexBuffer,
								 idxbufsize);
			m_vertbufs[device].indexBuffer.map();
		}
	}

	//draw
	void VolumeRenderer::draw(
		const std::unique_ptr<vks::VFrameBuffer>& framebuf,
		bool clear_framebuf,
		VSemaphoreSettings semaphores,
		bool draw_wireframe_p, bool interactive_mode_p,
		bool orthographic_p, double zoom, int mode, double sampling_frq_fac)
	{
		draw_volume(framebuf, clear_framebuf, semaphores, interactive_mode_p, orthographic_p, zoom, mode, sampling_frq_fac);
		//if(draw_wireframe_p)
		//	draw_wireframe(orthographic_p);
	}

	void VolumeRenderer::draw_volume(
		const std::unique_ptr<vks::VFrameBuffer>& framebuf,
		bool clear_framebuf,
		VSemaphoreSettings semaphores,
		bool interactive_mode_p, bool orthographic_p, double zoom, int mode, double sampling_frq_fac)
	{
		if (!tex_ || !m_vulkan)
			return;

		//comment off when debug_ds
		if (mem_swap_)
		{
			uint32_t rn_time = GET_TICK_COUNT();
			if (rn_time - st_time_ > get_up_time())
				return;
		}

		Ray view_ray = compute_view();
		Ray snapview = compute_snapview(0.4);

		vector<TextureBrick*>* bricks = 0;
		/*		if (mem_swap_ && interactive_)
					bricks = tex_->get_closest_bricks(
					quota_center_, quota_bricks_chan_, true,
					view_ray, orthographic_p);
				else
		*/			bricks = tex_->get_sorted_bricks(view_ray, orthographic_p);
		if (!bricks || bricks->size() == 0)
			return;

		set_interactive_mode(adaptive_ && interactive_mode_p);

		// Set sampling rate based on interaction
		double rate = imode_ ? irate_ * 2.0 : sampling_rate_ * 2.0;
		double rate_fac = 1.0;
		Vector diag = tex_->bbox()->diagonal();
		/*		Vector cell_diag(
					diag.x() / tex_->nx(),
					diag.y() / tex_->ny(),
					diag.z() / tex_->nz());
				double dt = cell_diag.length()/compute_rate_scale()/rate;
		*/		//double dt = 0.0020/rate * compute_dt_fac(sampling_frq_fac, &rate_fac);
		uint32_t w = m_vulkan->width;
		uint32_t h = m_vulkan->height;
		uint32_t minwh = min(w, h);
		uint32_t w2 = w;
		uint32_t h2 = h;
		
		double dt = compute_dt_fac_1px(w, h, sampling_frq_fac) / rate;
		num_slices_ = (int)(tex_->GetBrickIdSpaceMaxExtent().length() / dt);

		vector<float> vertex;
		vector<uint32_t> index;
		vector<uint32_t> size;
		vertex.reserve((size_t)num_slices_ * 12);
		index.reserve((size_t)num_slices_ * 6);
		size.reserve((size_t)num_slices_ * 6);

		//--------------------------------------------------------------------------
		bool use_fog = m_use_fog && colormap_mode_ != 2;

		double sf = CalcScaleFactor(w, h, tex_->nx(), tex_->ny(), zoom);
		if (fabs(sf - sfactor_) > 0.05)
		{
			sfactor_ = sf;
			//			blend_framebuffer_resize_ = true;
			//			filter_buffer_resize_ = true;
		}
		else if (sf == 1.0 && sfactor_ != 1.0)
		{
			sfactor_ = sf;
			//			blend_framebuffer_resize_ = true;
			//			filter_buffer_resize_ = true;
		}

		w2 = int(w/**sfactor_*/ * buffer_scale_ + 0.5);
		h2 = int(h/**sfactor_*/ * buffer_scale_ + 0.5);

		vks::VulkanDevice* prim_dev = m_vulkan->devices[0];

		if (m_volUniformBuffers.empty())
			m_vulkan->vol_shader_factory_->prepareUniformBuffers(m_volUniformBuffers);

		eval_ml_mode();

		VVolPipeline pipeline = prepareVolPipeline(prim_dev, mode_, update_order_, colormap_mode_);
		VkPipelineLayout pipelineLayout = m_vulkan->vol_shader_factory_->pipeline_[prim_dev].pipelineLayout;
		
		if (blend_num_bits_ > 8)
		{
			if (blend_framebuffer_ && 
				(blend_framebuffer_resize_ || pipeline.renderpass != blend_framebuffer_->renderPass))
			{
				blend_framebuffer_.reset();
				blend_tex_id_.reset();

				if (blend_framebuffer_resize_) blend_framebuffer_resize_ = false;
			}

			if (!blend_framebuffer_ )
			{
				blend_framebuffer_ = std::make_unique<vks::VFrameBuffer>(vks::VFrameBuffer());
				blend_framebuffer_->w = w2;
				blend_framebuffer_->h = h2;
				blend_framebuffer_->device = prim_dev;

				if (!blend_tex_id_)
					blend_tex_id_ = prim_dev->GenTexture2D(VK_FORMAT_R32G32B32A32_SFLOAT, VK_FILTER_LINEAR, w2, h2);

				blend_framebuffer_->addAttachment(blend_tex_id_);

				blend_framebuffer_->setup(pipeline.renderpass);
			}
		}

		std::vector<VkWriteDescriptorSet> descriptorWrites;
		m_vulkan->vol_shader_factory_->getDescriptorSetWriteUniforms(prim_dev, m_volUniformBuffers[prim_dev], descriptorWrites);
		if (colormap_mode_ == 3)
		{
			auto palette = get_palette();
			if (palette[0])
				descriptorWrites.push_back(VolShaderFactory::writeDescriptorSetTex(VK_NULL_HANDLE, 7, &palette[prim_dev]->descriptor));
		}

		VolShaderFactory::VolVertShaderUBO vert_ubo;
		VolShaderFactory::VolFragShaderBaseUBO frag_ubo;
		VolShaderFactory::VolFragShaderBrickConst frag_const;

		Vector light = view_ray.direction();
		light.safe_normalize();

		frag_ubo.loc0_light_alpha = { light.x(), light.y(), light.z(), alpha_ };
		if (shading_)
			frag_ubo.loc1_material = { 2.0 - ambient_, diffuse_, specular_, shine_ };
		else
			frag_ubo.loc1_material = { 2.0 - ambient_, 0.0, specular_, shine_ };

		//spacings
		double spcx, spcy, spcz;
		tex_->get_spacings(spcx, spcy, spcz);
		if (colormap_mode_ == 3)
		{
			int max_id = (*bricks)[0]->nb(0) == 2 ? USHRT_MAX : UCHAR_MAX;
			frag_ubo.loc5_spc_id = { spcx, spcy, spcz, max_id };
		}
		else
			frag_ubo.loc5_spc_id = { spcx, spcy, spcz, 1.0 };

		//transfer function
		frag_ubo.loc2_scscale_th = { inv_ ? -scalar_scale_ : scalar_scale_, gm_scale_, lo_thresh_, hi_thresh_ };
		frag_ubo.loc3_gamma_offset = { 1.0 / gamma3d_, gm_thresh_, offset_, sw_ };
		switch (colormap_mode_)
		{
		case 0://normal
			if (mask_ && !label_)
				frag_ubo.loc6_colparam = { mask_color_.r(), mask_color_.g(), mask_color_.b(), mask_thresh_ };
			else
				frag_ubo.loc6_colparam = { color_.r(), color_.g(), color_.b(), 0.0 };
			break;
		case 1://colormap
			frag_ubo.loc6_colparam = { colormap_low_value_, colormap_hi_value_, colormap_hi_value_ - colormap_low_value_, 0.0 };
			break;
		case 2://depth map
			frag_ubo.loc6_colparam = { color_.r(), color_.g(), color_.b(), 0.0 };
			break;
		case 3://indexed color
			HSVColor hsv(color_);
			double luminance = hsv.val();
			frag_ubo.loc6_colparam = { 1.0 / double(w2), 1.0 / double(h2), luminance, 0.0 };
			break;
		}

		//setup depth peeling
		if (depth_peel_ || colormap_mode_ == 2)
			frag_ubo.loc7_view = { 1.0 / double(w2), 1.0 / double(h2), 0.0, 0.0 };

		//fog
		if (m_use_fog)
			frag_ubo.loc8_fog = { m_fog_intensity, m_fog_start, m_fog_end, 0.0 };

		//set clipping planes
		double abcd[4];
		planes_[0]->get(abcd);
		frag_ubo.plane0 = { abcd[0], abcd[1], abcd[2], abcd[3] };
		planes_[1]->get(abcd);
		frag_ubo.plane1 = { abcd[0], abcd[1], abcd[2], abcd[3] };
		planes_[2]->get(abcd);
		frag_ubo.plane2 = { abcd[0], abcd[1], abcd[2], abcd[3] };
		planes_[3]->get(abcd);
		frag_ubo.plane3 = { abcd[0], abcd[1], abcd[2], abcd[3] };
		planes_[4]->get(abcd);
		frag_ubo.plane4 = { abcd[0], abcd[1], abcd[2], abcd[3] };
		planes_[5]->get(abcd);
		frag_ubo.plane5 = { abcd[0], abcd[1], abcd[2], abcd[3] };

		// Set up transform
		Transform* tform = tex_->transform();
		double mvmat[16];
		tform->get_trans(mvmat);
		m_mv_mat2 = glm::mat4(
			mvmat[0], mvmat[4], mvmat[8], mvmat[12],
			mvmat[1], mvmat[5], mvmat[9], mvmat[13],
			mvmat[2], mvmat[6], mvmat[10], mvmat[14],
			mvmat[3], mvmat[7], mvmat[11], mvmat[15]);
		m_mv_mat2 = m_mv_mat * m_mv_mat2;
		vert_ubo.proj_mat = m_proj_mat;
		vert_ubo.mv_mat = m_mv_mat2;

		VolShaderFactory::updateUniformBuffers(m_volUniformBuffers[prim_dev], vert_ubo, frag_ubo);

		bool clear = false;
		bool waitsemaphore = semaphores.wait_before_brk_rendering;
		if (!mem_swap_ || (mem_swap_ && cur_chan_brick_num_ == 0))
		{
			prepareVertexBuffers(prim_dev, dt);
			vertbuf_offset = 0;
			idxbuf_offset = 0;
			clear = true;
		}


		//////////////////////////////////////////
		//render bricks

		int bmode = mode;
		if (mask_)  bmode = TEXTURE_RENDER_MODE_MASK;
		if (label_) bmode = TEXTURE_RENDER_MODE_LABEL;

		VkCommandBuffer cmdbuf = m_commandBuffers[prim_dev];
		if (!mem_swap_)
		{
			VkCommandBufferBeginInfo cmdBufInfo = vks::initializers::commandBufferBeginInfo();
			cmdBufInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

			VkRenderPassBeginInfo renderPassBeginInfo = vks::initializers::renderPassBeginInfo();
			renderPassBeginInfo.renderPass = pipeline.renderpass;
			renderPassBeginInfo.renderArea.offset.x = 0;
			renderPassBeginInfo.renderArea.offset.y = 0;
			renderPassBeginInfo.renderArea.extent.width = blend_framebuffer_->w;
			renderPassBeginInfo.renderArea.extent.height = blend_framebuffer_->h;

			renderPassBeginInfo.framebuffer = blend_framebuffer_->framebuffer;

			VK_CHECK_RESULT(vkBeginCommandBuffer(cmdbuf, &cmdBufInfo));

			vkCmdBeginRenderPass(cmdbuf, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);
		}
		
		int count = 0;
		int que = 4;
		for (unsigned int i=0; i < bricks->size(); i++)
		{
			TextureBrick* b = (*bricks)[i];
						
			if (mem_swap_ && start_update_loop_ && !done_update_loop_)
			{
				if (b->drawn(bmode))
					continue;
			}

			if (!b->get_disp() || // Clip against view
				b->get_priority()>0) //nothing to draw
			{
				if (mem_swap_ && start_update_loop_ && !done_update_loop_)
				{
					if (!b->drawn(bmode))
					{
						b->set_drawn(bmode, true);
						//cur_brick_num_++;
						cur_chan_brick_num_++;
					}
				}
				continue;
			}

			vertex.clear();
			index.clear();
			size.clear();
			b->compute_polygons(view_ray, dt, vertex, index, size);

			if (vertex.size() == 0){
				if (mem_swap_ && start_update_loop_ && !done_update_loop_)
				{
					if (!b->drawn(bmode))
					{
						b->set_drawn(bmode, true);
						cur_brick_num_++;
						cur_chan_brick_num_++;
					}
				}
				continue;
			}

			memcpy( (unsigned char*)m_vertbufs[prim_dev].vertexBuffer.mapped + vertbuf_offset,
					vertex.data(), 
					vertex.size()*sizeof(float) );
			memcpy( (unsigned char*)m_vertbufs[prim_dev].indexBuffer.mapped + idxbuf_offset,
					index.data(),
					index.size() * sizeof(unsigned int));

			VkFilter filter;
			if (interpolate_ && colormap_mode_ != 3)
				filter = VK_FILTER_LINEAR;
			else
				filter = VK_FILTER_NEAREST;

			shared_ptr<vks::VTexture> brktex, msktex, lbltex;
			brktex = load_brick(prim_dev, 0, 0, bricks, i, filter, compression_, mode, (mask_ || label_) ? false : true);
			if (!brktex)
				continue;
			b->prevent_tex_deletion(true);
			if (mask_)
				msktex = load_brick_mask(prim_dev, bricks, i, filter, false, 0, true);
			else if (tex_->nmask() != -1 && m_mask_hide_mode != VOL_MASK_HIDE_NONE)
			{
#ifndef _UNIT_TEST_VOLUME_RENDERER_
				if (tex_->isBrxml() && tex_->GetMaskLv() != tex_->GetCurLevel())
				{
					m_pThreadCS.Enter();

					int cnx = b->nx(), cny = b->ny(), cnz = b->nz();
					int cox = b->ox(), coy = b->oy(), coz = b->oz();
					int cmx = b->mx(), cmy = b->my(), cmz = b->mz();
					int csx = b->sx(), csy = b->sy(), csz = b->sz();
					Nrrd *cdata = b->get_nrrd(0);
					Nrrd *cmdata = b->get_nrrd(tex_->nmask());
					
					b->set_nrrd(tex_->get_nrrd_lv(tex_->GetMaskLv(),0), 0);
					b->set_nrrd(tex_->get_nrrd(tex_->nmask()), tex_->nmask());
					int sx = b->sx(), sy = b->sy(), sz = b->sz();
					double xscale = (double)sx/csx;
					double yscale = (double)sy/csy;
					double zscale = (double)sz/csz;
					
					double ox_d = (double)cox*xscale;
					double oy_d = (double)coy*yscale;
					double oz_d = (double)coz*zscale;
					int ox = (int)floor(ox_d);
					int oy = (int)floor(oy_d);
					int oz = (int)floor(oz_d);

					b->ox(ox); b->oy(oy); b->oz(oz);

					double endx_d = (double)(cox+cnx)*xscale;
					double endy_d = (double)(coy+cny)*yscale;
					double endz_d = (double)(coz+cnz)*zscale;
					int nx = (int)ceil(endx_d) - ox;
					int ny = (int)ceil(endy_d) - oy;
					int nz = (int)ceil(endz_d) - oz;
					
					b->nx(nx); b->ny(ny); b->nz(nz);
					b->mx(nx); b->my(ny); b->mz(nz);

					msktex = load_brick_mask(prim_dev, bricks, i, filter, false, 0, true, false);

					double trans_x = (ox_d-ox)/nx;
					double trans_y = (oy_d-oy)/ny;
					double trans_z = (oz_d-oz)/nz;
					double scale_x = (endx_d-ox_d)/nx;
					double scale_y = (endy_d-oy_d)/ny;
					double scale_z = (endz_d-oz_d)/nz;

					frag_const.mask_b_scale = { (float)scale_x, (float)scale_y, (float)scale_z, 1.0f };
					frag_const.mask_b_trans = { (float)trans_x, (float)trans_y, (float)trans_z, 0.0f };

					b->nx(cnx); b->ny(cny); b->nz(cnz);
					b->ox(cox); b->oy(coy); b->oz(coz);
					b->mx(cmx); b->my(cmy); b->mz(cmz);
					b->set_nrrd(cdata, 0);
					b->set_nrrd(cmdata, tex_->nmask());

					m_pThreadCS.Leave();
				}
				else
				{
					msktex = load_brick_mask(prim_dev, bricks, i, filter, false, 0, true, false);

					frag_const.mask_b_scale = { 1.0f, 1.0f, 1.0f, 1.0f };
					frag_const.mask_b_trans = { 0.0f, 0.0f, 0.0f, 0.0f };
				}
#endif
			}

			if (label_)
				lbltex = load_brick_label(prim_dev, bricks, i);

			b->prevent_tex_deletion(false);

			descriptorWrites.push_back(VolShaderFactory::writeDescriptorSetTex(VK_NULL_HANDLE, 0, &brktex->descriptor));
			if (msktex)
				descriptorWrites.push_back(VolShaderFactory::writeDescriptorSetTex(VK_NULL_HANDLE, 2, &brktex->descriptor));
			if (lbltex)
				descriptorWrites.push_back(VolShaderFactory::writeDescriptorSetTex(VK_NULL_HANDLE, 3, &brktex->descriptor));

			frag_const.loc_dim_inv = { 1.0 / b->nx(), 1.0 / b->ny(), 1.0 / b->nz(),
									   mode_ == MODE_OVER ? 1.0 / (rate * minwh * 0.001 * zoom * 2.0) : 1.0 };
			//for brick transformation
			BBox dbox = b->dbox();
			frag_const.b_scale = { float(dbox.max().x() - dbox.min().x()),
								   float(dbox.max().y() - dbox.min().y()),
								   float(dbox.max().z() - dbox.min().z()),
								   1.0f };

			frag_const.b_trans = { float(dbox.min().x()), float(dbox.min().y()), float(dbox.min().z()), 0.0f };



			//////////////////////
			//build command buffer
			if (mem_swap_ && count == 0)
			{
				VkCommandBufferBeginInfo cmdBufInfo = vks::initializers::commandBufferBeginInfo();
				cmdBufInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

				VkRenderPassBeginInfo renderPassBeginInfo = vks::initializers::renderPassBeginInfo();
				renderPassBeginInfo.renderPass = pipeline.renderpass;
				renderPassBeginInfo.renderArea.offset.x = 0;
				renderPassBeginInfo.renderArea.offset.y = 0;
				renderPassBeginInfo.renderArea.extent.width = blend_framebuffer_->w;
				renderPassBeginInfo.renderArea.extent.height = blend_framebuffer_->h;

				renderPassBeginInfo.framebuffer = blend_framebuffer_->framebuffer;

				VK_CHECK_RESULT(vkBeginCommandBuffer(cmdbuf, &cmdBufInfo));

				vkCmdBeginRenderPass(cmdbuf, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);
			}

			VkViewport viewport = vks::initializers::viewport((float)w2, (float)h2, 0.0f, 1.0f);
			vkCmdSetViewport(cmdbuf, 0, 1, &viewport);

			VkRect2D scissor = vks::initializers::rect2D(w2, h2, 0, 0);
			vkCmdSetScissor(cmdbuf, 0, 1, &scissor);

			if (!descriptorWrites.empty())
			{
				prim_dev->vkCmdPushDescriptorSetKHR(
					cmdbuf,
					VK_PIPELINE_BIND_POINT_GRAPHICS,
					pipelineLayout,
					0,
					descriptorWrites.size(),
					descriptorWrites.data()
				);
			}

			if (clear)
			{
				VkClearValue clearValues[1];
				clearValues[0].color = m_clear_color;

				VkClearAttachment clearAttachments[1] = {};
				clearAttachments[0].aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
				clearAttachments[0].clearValue = clearValues[0];
				clearAttachments[0].colorAttachment = 0;

				VkClearRect clearRect[1] = {};
				clearRect[0].layerCount = 1;
				clearRect[0].rect.offset = { 0, 0 };
				clearRect[0].rect.extent = { w2, h2 };

				vkCmdClearAttachments(
					cmdbuf,
					1,
					clearAttachments,
					1,
					clearRect);

				clear = false;
			}

			vkCmdBindPipeline(cmdbuf, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.vkpipeline);

			vkCmdPushConstants(
				cmdbuf,
				pipelineLayout,
				VK_SHADER_STAGE_FRAGMENT_BIT,
				0,
				sizeof(VolShaderFactory::VolFragShaderBrickConst),
				&frag_const);

			vkCmdBindVertexBuffers(cmdbuf, 0, 1, &m_vertbufs[prim_dev].vertexBuffer.buffer, &vertbuf_offset);
			vkCmdBindIndexBuffer(cmdbuf, m_vertbufs[prim_dev].indexBuffer.buffer, idxbuf_offset, VK_INDEX_TYPE_UINT32);
			vkCmdDrawIndexed(cmdbuf, (uint32_t)index.size(), 1, 0, 0, 0);

			vertbuf_offset += vertex.size() * sizeof(float);
			idxbuf_offset += index.size() * sizeof(unsigned int);

			count++;
			if (mem_swap_ && (count >= que || i >= bricks->size()-1) )
			{
				count = 0;

				vkCmdEndRenderPass(cmdbuf);

				VK_CHECK_RESULT(vkEndCommandBuffer(cmdbuf));

				VkSubmitInfo submitInfo = vks::initializers::submitInfo();
				submitInfo.commandBufferCount = 1;
				submitInfo.pCommandBuffers = &cmdbuf;

				std::vector<VkPipelineStageFlags> waitStages;
				if (waitsemaphore && semaphores.waitSemaphoreCount > 0)
				{
					for (uint32_t i = 0; i < semaphores.waitSemaphoreCount; i++)
						waitStages.push_back(VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
					submitInfo.waitSemaphoreCount = semaphores.waitSemaphoreCount;
					submitInfo.pWaitSemaphores = semaphores.waitSemaphores;
					submitInfo.pWaitDstStageMask = waitStages.data();
					waitsemaphore = false;
				}
				
				VkFenceCreateInfo fenceInfo = vks::initializers::fenceCreateInfo(VK_FLAGS_NONE);
				VkFence fence;
				VK_CHECK_RESULT(vkCreateFence(prim_dev->logicalDevice, &fenceInfo, nullptr, &fence));

				// Submit to the queue
				VK_CHECK_RESULT(vkQueueSubmit(prim_dev->queue, 1, &submitInfo, fence));
				// Wait for the fence to signal that command buffer has finished executing
				VK_CHECK_RESULT(vkWaitForFences(prim_dev->logicalDevice, 1, &fence, VK_TRUE, DEFAULT_FENCE_TIMEOUT));

				vkDestroyFence(prim_dev->logicalDevice, fence, nullptr);

				finished_bricks_++;
			}

			//comment off when debug_ds
			if (mem_swap_/* && !mask_ && !label_*/ && count == 0)
			{
				uint32_t rn_time = GET_TICK_COUNT();
				if (rn_time - st_time_ > get_up_time())
					break;
			}
		}

		if (!mem_swap_)
		{
			vkCmdEndRenderPass(cmdbuf);

			VK_CHECK_RESULT(vkEndCommandBuffer(cmdbuf));

			VkSubmitInfo submitInfo = vks::initializers::submitInfo();
			submitInfo.commandBufferCount = 1;
			submitInfo.pCommandBuffers = &cmdbuf;
			submitInfo.signalSemaphoreCount = 1;
			submitInfo.pSignalSemaphores = &m_volFinishedSemaphores[prim_dev];

			std::vector<VkPipelineStageFlags> waitStages;
			if (waitsemaphore && semaphores.waitSemaphoreCount > 0)
			{
				for (uint32_t i = 0; i < semaphores.waitSemaphoreCount; i++)
					waitStages.push_back(VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
				submitInfo.waitSemaphoreCount = semaphores.waitSemaphoreCount;
				submitInfo.pWaitSemaphores = semaphores.waitSemaphores;
				submitInfo.pWaitDstStageMask = waitStages.data();
				waitsemaphore = false;
			}

			// Submit to the queue
			VK_CHECK_RESULT(vkQueueSubmit(prim_dev->queue, 1, &submitInfo, VK_NULL_HANDLE));

			//VK_CHECK_RESULT(vkQueueWaitIdle(prim_dev->queue));
			//saveScreenshot("E:\\vulkan_screenshot.ppm");
		}

		if (mem_swap_ &&
			cur_brick_num_ == total_brick_num_)
		{
			done_update_loop_ = true;
			done_current_chan_ = true;
			clear_chan_buffer_ = true;
			save_final_buffer_ = true;
			cur_chan_brick_num_ = 0;
			done_loop_[bmode] = true;
		}

		if (mem_swap_ &&
			(size_t)cur_chan_brick_num_ == (*bricks).size())
		{
			done_current_chan_ = true;
			clear_chan_buffer_ = true;
			save_final_buffer_ = true;
			cur_chan_brick_num_ = 0;
			done_loop_[bmode] = true;
		}

		////////////////////////////////////////////////////////
		//output result
		if (blend_num_bits_ > 8)
		{
			ShaderProgram* img_shader = 0;

			if (noise_red_ && colormap_mode_!=2)
			{
				//FILTERING/////////////////////////////////////////////////////////////////
				filter_size_min_ = CalcFilterSize(4, w, h, tex_->nx(), tex_->ny(), zoom, sfactor_);

				Vulkan2dRender::V2DRenderParams filter_params;
				filter_params.pipeline =
					m_v2drender->preparePipeline(
						IMG_SHDR_FILTER_BLUR,
						V2DRENDER_BLEND_OVER,
						VK_FORMAT_R32G32B32A32_SFLOAT,
						1,
						filter_buffer_->attachments[0]->is_swapchain_images);

				if (filter_buffer_ &&
					(filter_buffer_resize_ || filter_buffer_->renderPass != filter_params.pipeline.pass))
				{
					filter_buffer_.reset();
					filter_tex_id_.reset();

					filter_buffer_resize_ = false;
				}
				if (!filter_buffer_)
				{
					filter_buffer_ = std::make_unique<vks::VFrameBuffer>(vks::VFrameBuffer());
					filter_buffer_->w = w2;
					filter_buffer_->h = h2;
					filter_buffer_->device = prim_dev;

					if (!filter_tex_id_)
						filter_tex_id_ = prim_dev->GenTexture2D(VK_FORMAT_R32G32B32A32_SFLOAT, VK_FILTER_LINEAR, w2, h2);

					filter_buffer_->addAttachment(blend_tex_id_);

					filter_buffer_->setup(filter_params.pipeline.pass);
				}

				filter_params.clear = true;
				filter_params.loc[0] = { filter_size_min_/w2, filter_size_min_/h2, 1.0/w2, 1.0/h2 };
				filter_params.tex[0] = blend_tex_id_.get();
				if (!mem_swap_)
				{
					filter_params.waitSemaphoreCount = 1;
					filter_params.waitSemaphores = &m_volFinishedSemaphores[prim_dev];
				}
				filter_params.signalSemaphoreCount = 1;
				filter_params.signalSemaphores = &m_filterFinishedSemaphores[prim_dev];

				m_v2drender->render(filter_buffer_, filter_params);
			}

			Vulkan2dRender::V2DRenderParams params;
			std::vector<VkSemaphore> waitsemaphores;

			if (noise_red_ && colormap_mode_ != 2)
			{
				params.pipeline =
					m_v2drender->preparePipeline(
						IMG_SHDR_FILTER_SHARPEN,
						V2DRENDER_BLEND_OVER,
						framebuf->attachments[0]->format,
						framebuf->attachments.size(),
						framebuf->attachments[0]->is_swapchain_images);
				params.tex[0] = filter_tex_id_.get();
				filter_size_shp_ = CalcFilterSize(3, w, h, tex_->nx(), tex_->ny(), zoom, sfactor_);
				params.loc[0] = { filter_size_shp_/w, filter_size_shp_/h, 0.0, 0.0 };
				waitsemaphores.push_back(m_filterFinishedSemaphores[prim_dev]);
			}
			else
			{
				params.pipeline =
					m_v2drender->preparePipeline(
						IMG_SHADER_TEXTURE_LOOKUP,
						V2DRENDER_BLEND_OVER,
						framebuf->attachments[0]->format,
						framebuf->attachments.size(),
						framebuf->attachments[0]->is_swapchain_images);
				params.tex[0] = blend_tex_id_.get();
				if (!mem_swap_)
				{
					waitsemaphores.push_back(m_volFinishedSemaphores[prim_dev]);
				}
			}
			std::vector<VkPipelineStageFlags> waitStages;
			if (!semaphores.wait_before_brk_rendering && semaphores.waitSemaphoreCount > 0)
			{
				for (int s = 0; s < semaphores.waitSemaphoreCount; s++)
				{
					waitsemaphores.push_back(semaphores.waitSemaphores[s]);
				}
			}

			if (!waitsemaphores.empty())
			{
				params.waitSemaphoreCount = waitsemaphores.size();
				params.waitSemaphores = waitsemaphores.data();
			}
			params.signalSemaphoreCount = semaphores.signalSemaphoreCount;
			params.signalSemaphores = semaphores.signalSemaphores;

			params.clear = clear_framebuf;

			framebuf->replaceRenderPass(params.pipeline.pass);
			
			m_v2drender->render(framebuf, params);
			
			VK_CHECK_RESULT(vkQueueWaitIdle(prim_dev->queue));
		}

	}
	//
	//void VolumeRenderer::draw_wireframe(bool orthographic_p, double sampling_frq_fac)
	//{
	//	Ray view_ray = compute_view();
	//	Ray snapview = compute_snapview(0.4);

	//	glEnable(GL_DEPTH_TEST);
	//	vector<TextureBrick*> *bricks = tex_->get_sorted_bricks(view_ray, orthographic_p);

	//	float rate = imode_ ? irate_ : sampling_rate_;
	//	double rate_fac = 1.0;
	//	Vector diag = tex_->bbox()->diagonal();
	//	Vector cell_diag(diag.x()/tex_->nx(),
	//		diag.y()/tex_->ny(),
	//		diag.z()/tex_->nz());
	//	double dt = 0.0025/rate * compute_dt_fac(sampling_frq_fac, &rate_fac);
	//	num_slices_ = (int)(diag.length()/dt);

	//	vector<float> vertex;
	//	vector<uint32_t> index;
	//	vector<uint32_t> size;
	//	vertex.reserve(num_slices_*12);
	//	index.reserve(num_slices_*6);
	//	size.reserve(num_slices_*6);

	//	// Set up shaders
	//	ShaderProgram* shader = 0;
	//	//create/bind
	//	shader = vol_shader_factory_.shader(
	//		true, 0,
	//		false, false,
	//		0, false,
	//		false, 0,
	//		0, 0, 0,
	//		false, 1, 0);
	//	if (shader)
	//	{
	//		if (!shader->valid())
	//			shader->create();
	//		shader->bind();
	//	}

	//	////////////////////////////////////////////////////////
	//	// render bricks
	//	// Set up transform
	//	Transform *tform = tex_->transform();
	//	double mvmat[16];
	//	tform->get_trans(mvmat);
	//	m_mv_mat2 = glm::mat4(
	//		mvmat[0], mvmat[4], mvmat[8], mvmat[12],
	//		mvmat[1], mvmat[5], mvmat[9], mvmat[13],
	//		mvmat[2], mvmat[6], mvmat[10], mvmat[14],
	//		mvmat[3], mvmat[7], mvmat[11], mvmat[15]);
	//	m_mv_mat2 = m_mv_mat * m_mv_mat2;
	//	shader->setLocalParamMatrix(0, glm::value_ptr(m_proj_mat));
	//	shader->setLocalParamMatrix(1, glm::value_ptr(m_mv_mat2));
	//	shader->setLocalParamMatrix(5, glm::value_ptr(m_tex_mat));
	//	shader->setLocalParam(0, color_.r(), color_.g(), color_.b(), 1.0);

	//	if (bricks)
	//	{
	//		for (unsigned int i=0; i<bricks->size(); i++)
	//		{
	//			TextureBrick* b = (*bricks)[i];
	//			if (!b->get_disp()) continue;

	//			vertex.clear();
	//			index.clear();
	//			size.clear();

	//			// Scale out dt such that the slices are artificially further apart.
	//			b->compute_polygons(view_ray, dt * 1, vertex, index, size);
	//			draw_polygons_wireframe(vertex, index, size);
	//		}
	//	}

	//	// Release shader.
	//	if (shader && shader->valid())
	//		shader->release();
	//}

	VolumeRenderer::VSegPipeline VolumeRenderer::prepareSegPipeline(vks::VulkanDevice* device, int type, int paint_mode, int hr_mode)
	{
		VSegPipeline ret_pipeline;

		bool use_2d = tex_2d_weight1_ && tex_2d_weight2_ ? true : false;
		bool use_stroke = (tex_->nstroke() >= 0) ? true : false;

		ShaderProgram* seg_shader = nullptr;
		switch (type)
		{
		case 0://initialize
			seg_shader = m_vulkan->seg_shader_factory_->shader(
				device->logicalDevice,
				SEG_SHDR_INITIALIZE, paint_mode, hr_mode,
				use_2d, true, depth_peel_, true, hiqual_, use_stroke);
			break;
		case 1://diffusion-based growing
			seg_shader = m_vulkan->seg_shader_factory_->shader(
				device->logicalDevice,
				SEG_SHDR_DB_GROW, paint_mode, hr_mode,
				use_2d, true, depth_peel_, true, hiqual_, use_stroke);
			break;
		case 2://noise removal
			seg_shader = m_vulkan->seg_shader_factory_->shader(
				device->logicalDevice,
				FLT_SHDR_NR, paint_mode, hr_mode,
				false, false, depth_peel_, false, hiqual_, use_stroke);
			break;
		}

		if (m_prev_seg_pipeline >= 0) {
			if (m_seg_pipelines[m_prev_seg_pipeline].device == device &&
				m_seg_pipelines[m_prev_seg_pipeline].shader == seg_shader)
				return m_seg_pipelines[m_prev_seg_pipeline];
		}
		for (int i = 0; i < m_seg_pipelines.size(); i++) {
			if (m_seg_pipelines[i].device == device &&
				m_seg_pipelines[i].shader == seg_shader)
			{
				m_prev_seg_pipeline = i;
				return m_seg_pipelines[i];
			}
		}

		VkComputePipelineCreateInfo computePipelineCreateInfo =
			vks::initializers::computePipelineCreateInfo(m_vulkan->seg_shader_factory_->pipeline_[device].pipelineLayout, 0);

		computePipelineCreateInfo.stage = seg_shader->get_compute_shader();
		VK_CHECK_RESULT(
			vkCreateComputePipelines(
				device->logicalDevice,
				device->pipelineCache,
				1, 
				&computePipelineCreateInfo,
				nullptr,
				&ret_pipeline.vkpipeline
			)
		);
		
		ret_pipeline.device = device;
		ret_pipeline.shader = seg_shader;
		
		m_seg_pipelines.push_back(ret_pipeline);
		m_prev_seg_pipeline = m_seg_pipelines.size() - 1;

		return ret_pipeline;
	}

	//type: 0-initial; 1-diffusion-based growing; 2-masked filtering
	//paint_mode: 1-select; 2-append; 3-erase; 4-diffuse; 5-flood; 6-clear; 7-all;
	//			  11-posterize
	//hr_mode (hidden removal): 0-none; 1-ortho; 2-persp
	void VolumeRenderer::draw_mask(
		VSemaphoreSettings semaphores,
		int type, int paint_mode, int hr_mode,
		double ini_thresh, double gm_falloff, double scl_falloff,
		double scl_translate, double w2d, double bins, bool orthographic_p,
		bool estimate)
	{
/*		if (paint_mode == 1 || paint_mode == 2)
		{
			draw_mask_cpu(type, paint_mode, hr_mode, ini_thresh, gm_falloff, scl_falloff, scl_translate, w2d, bins, orthographic_p, estimate);
			return;
		}
*/
		if (estimate && type==0)
			est_thresh_ = 0.0;
		bool use_2d = tex_2d_weight1_ && tex_2d_weight2_ ? true : false;

		bool use_stroke = (tex_->nstroke() >= 0 && (type == 0 || type == 1)) ? true : false;
		
		Ray view_ray = compute_view();

		vector<TextureBrick*> *bricks = tex_->get_sorted_bricks(view_ray, orthographic_p);
		if (!bricks || bricks->size() == 0)
			return;

		vks::VulkanDevice* prim_dev = m_vulkan->devices[0];

		if (m_segUniformBuffers.empty())
			m_vulkan->seg_shader_factory_->prepareUniformBuffers(m_segUniformBuffers);

		eval_ml_mode();

		VSegPipeline pipeline = prepareSegPipeline(prim_dev, type, paint_mode, hr_mode);
		VkPipelineLayout pipelineLayout = m_vulkan->seg_shader_factory_->pipeline_[prim_dev].pipelineLayout;

		std::vector<VkWriteDescriptorSet> descriptorWrites;
		m_vulkan->seg_shader_factory_->getDescriptorSetWriteUniforms(prim_dev, m_segUniformBuffers[prim_dev], descriptorWrites);
		
		SegShaderFactory::SegCompShaderBaseUBO seg_ubo;
		SegShaderFactory::SegCompShaderBrickConst seg_const;

		//set uniforms
		//set up shading
		Vector light = compute_view().direction();
		light.safe_normalize();
		seg_ubo.loc0_light_alpha = { light.x(), light.y(), light.z(), alpha_ };
		if (shading_)
			seg_ubo.loc0_light_alpha = { 2.0 - ambient_, diffuse_, specular_, shine_ };
		else
			seg_ubo.loc0_light_alpha = { 2.0 - ambient_, 0.0, specular_, shine_ };

		//spacings
		double spcx, spcy, spcz;
		tex_->get_spacings(spcx, spcy, spcz);
		seg_ubo.loc5_spc_id = { spcx, spcy, spcz, 1.0 };

		//transfer function
		seg_ubo.loc2_scscale_th = { inv_ ? -scalar_scale_ : scalar_scale_, gm_scale_, lo_thresh_, hi_thresh_ };
		seg_ubo.loc3_gamma_offset = { 1.0 / gamma3d_, gm_thresh_, offset_, sw_ };
		seg_ubo.loc6_colparam = { color_.r(), color_.g(), color_.b(), 0.0 };

		//thresh1
		seg_ubo.loc7_th = { ini_thresh, gm_falloff, scl_falloff, scl_translate };
		//w2d
		seg_ubo.loc8_w2d = { w2d, bins, 0.0, 0.0 };

		//set clipping planes
		double abcd[4];
		planes_[0]->get(abcd);
		seg_ubo.plane0 = { abcd[0], abcd[1], abcd[2], abcd[3] };
		planes_[1]->get(abcd);
		seg_ubo.plane1 = { abcd[0], abcd[1], abcd[2], abcd[3] };
		planes_[2]->get(abcd);
		seg_ubo.plane2 = { abcd[0], abcd[1], abcd[2], abcd[3] };
		planes_[3]->get(abcd);
		seg_ubo.plane3 = { abcd[0], abcd[1], abcd[2], abcd[3] };
		planes_[4]->get(abcd);
		seg_ubo.plane4 = { abcd[0], abcd[1], abcd[2], abcd[3] };
		planes_[5]->get(abcd);
		seg_ubo.plane5 = { abcd[0], abcd[1], abcd[2], abcd[3] };

		////////////////////////////////////////////////////////
		// render bricks
		// Set up transform
		Transform *tform = tex_->transform();
		double mvmat[16];
		tform->get_trans(mvmat);
		m_mv_mat2 = glm::mat4(
			mvmat[0], mvmat[4], mvmat[8], mvmat[12],
			mvmat[1], mvmat[5], mvmat[9], mvmat[13],
			mvmat[2], mvmat[6], mvmat[10], mvmat[14],
			mvmat[3], mvmat[7], mvmat[11], mvmat[15]);
		seg_ubo.mv_mat = m_mv_mat * m_mv_mat2;
		seg_ubo.proj_mat = m_proj_mat;
		if (hr_mode > 0)
		{
			seg_ubo.mv_mat_inv = glm::inverse(m_mv_mat2);
		}

		//bind 2d mask texture
		if (tex_2d_mask_)
			descriptorWrites.push_back(SegShaderFactory::writeDescriptorSetTex(VK_NULL_HANDLE, 6, &tex_2d_mask_->descriptor));
		//bind 2d weight map
		if (use_2d)
		{
			if (tex_2d_weight1_)
				descriptorWrites.push_back(SegShaderFactory::writeDescriptorSetTex(VK_NULL_HANDLE, 4, &tex_2d_weight1_->descriptor));
			if (tex_2d_weight2_)
				descriptorWrites.push_back(SegShaderFactory::writeDescriptorSetTex(VK_NULL_HANDLE, 5, &tex_2d_weight2_->descriptor));
		}

		bool clear_stroke = (paint_mode == 1 || paint_mode == 2 || (paint_mode == 4 && type == 0) || paint_mode == 6 || paint_mode == 7);

		SegShaderFactory::updateUniformBuffers(m_segUniformBuffers[prim_dev], seg_ubo);

		bool waitsemaphore = semaphores.wait_before_brk_rendering;

		bool write_to_vol = false;
		switch (type)
		{
		case 0:
		case 1:
			write_to_vol = false;
			break;
		case 2:
			write_to_vol = true;
			break;
		}
		
		//////////////////////////////////////////
		//render bricks

		// Flush the queue if we're rebuilding the command buffer after a pipeline change to ensure it's not currently in use
		vkQueueWaitIdle(prim_dev->compute_queue);

		VkCommandBuffer cmdbuf = m_seg_commandBuffers[prim_dev];
		VkCommandBufferBeginInfo cmdBufInfo = vks::initializers::commandBufferBeginInfo();
		VK_CHECK_RESULT(vkBeginCommandBuffer(cmdbuf, &cmdBufInfo));

		vkCmdBindPipeline(cmdbuf, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline.vkpipeline);
		
		for (unsigned int i=0; i < bricks->size(); i++)
		{
			shared_ptr<vks::VTexture> brktex, msktex, stroketex;
			
			b->prevent_tex_deletion(true);
			brktex = load_brick(prim_dev, 0, 0, bricks, i, VK_FILTER_NEAREST, compression_);
			if (!brktex) continue;
			msktex = load_brick_mask(prim_dev, bricks, i, VK_FILTER_NEAREST, false, 0, true);
			if (!msktex) continue;
			if (use_stroke)
			{
				stroketex = load_brick_stroke(prim_dev, bricks, i, VK_FILTER_NEAREST, false, 0, true);
				if (!stroketex) continue;
			}
			b->prevent_tex_deletion(false);

			if (brktex)
				descriptorWrites.push_back(VolShaderFactory::writeDescriptorSetTex(VK_NULL_HANDLE, 0, &brktex->descriptor));
			/*if (msktex)
				descriptorWrites.push_back(VolShaderFactory::writeDescriptorSetTex(VK_NULL_HANDLE, 2, &brktex->descriptor));
			if (lbltex)
				descriptorWrites.push_back(VolShaderFactory::writeDescriptorSetTex(VK_NULL_HANDLE, 3, &brktex->descriptor));*/

			//draw each slice
			for (int z=0; z<b->nz(); z++)
			{
				glFramebufferTexture3D(GL_FRAMEBUFFER, 
					GL_COLOR_ATTACHMENT0,
					GL_TEXTURE_3D,
					tex_id,
					0,
					z);

				if (use_stroke && glIsTexture(stroke_tex_id) && (type == 0 || type == 1))
				{
					glFramebufferTexture3D(GL_FRAMEBUFFER, 
						GL_COLOR_ATTACHMENT1,
						GL_TEXTURE_3D,
						stroke_tex_id,
						0,
						z);
					if (clear_stroke)
					{
						glDrawBuffers(1, &attachment1);
						glClearColor(clear_color[0], clear_color[1], clear_color[2], clear_color[3]);
						glClear(GL_COLOR_BUFFER_BIT);
					}
					glDrawBuffers(2, draw_buffers);
				}
				else
					glDrawBuffers(1, &attachment0);
				

				draw_view_quad(double(z+0.5) / double(b->nz()));
			}

			//size and sample rate
			seg_const.loc_dim_inv = { 1.0 / b->nx(), 1.0 / b->ny(), 1.0 / b->nz(),
									  mode_ == MODE_OVER ? 1.0 / sampling_rate_ : 1.0 };
			//for brick transformation
			BBox dbox = b->dbox();
			seg_const.b_scale = { float(dbox.max().x() - dbox.min().x()),
								  float(dbox.max().y() - dbox.min().y()),
								  float(dbox.max().z() - dbox.min().z()),
								  1.0f };
			seg_const.b_trans = { float(dbox.min().x()), float(dbox.min().y()), float(dbox.min().z()), 0.0f };

			if (!descriptorWrites.empty())
			{
				prim_dev->vkCmdPushDescriptorSetKHR(
					cmdbuf,
					VK_PIPELINE_BIND_POINT_COMPUTE,
					pipelineLayout,
					0,
					descriptorWrites.size(),
					descriptorWrites.data()
				);
			}

			vkCmdPushConstants(
				cmdbuf,
				pipelineLayout,
				VK_SHADER_STAGE_COMPUTE_BIT,
				0,
				sizeof(SegShaderFactory::SegCompShaderBrickConst),
				&seg_const
			);

			vkCmdDispatch(compute.commandBuffer, textureComputeTarget.width / 16, textureComputeTarget.height / 16, 1);

			b->set_dirty(b->nmask(), true);

		}

		vkEndCommandBuffer(cmdbuf);

	}

//	void VolumeRenderer::draw_mask_cpu(int type, int paint_mode, int hr_mode,
//		double ini_thresh, double gm_falloff, double scl_falloff,
//		double scl_translate, double w2d, double bins, bool orthographic_p,
//		bool estimate)
//	{
//		if (estimate && type==0)
//			est_thresh_ = 0.0;
//		bool use_2d = glIsTexture(tex_2d_weight1_)&&
//			glIsTexture(tex_2d_weight2_)?true:false;
//
//		Ray view_ray = compute_view();
//
//		vector<TextureBrick*> *bricks = tex_->get_sorted_bricks(view_ray, orthographic_p);
//		if (!bricks || bricks->size() == 0)
//			return;
//
//		Nrrd *source = tex_->get_nrrd(0);
//		Nrrd *target = NULL;
//		switch (type)
//		{
//			case 0:
//			case 1:
//				target = tex_->get_nrrd(tex_->nmask());
//				break;
//			case 2:
//				//target = tex_->get_nrrd(0);
//				break;
//		}
//		if (!source || !target) return;
//
//		//spacings
//		double spcx, spcy, spcz;
//		tex_->get_spacings(spcx, spcy, spcz);
//
//		//transfer function
//		glm::vec4 loc2 = glm::vec4(inv_?-scalar_scale_:scalar_scale_, gm_scale_, lo_thresh_, hi_thresh_);
//
//		//setup depth peeling
//		//if (depth_peel_)
//		//	seg_shader->setLocalParam(7, 1.0/double(w2), 1.0/double(h2), 0.0, 0.0);
//
//		//thresh1
//		glm::vec4 loc7 = glm::vec4(ini_thresh, gm_falloff, scl_falloff, scl_translate);
//		//w2d
//		glm::vec4 loc8 = glm::vec4(w2d, bins, 0.0, 0.0);
//		
//		//set clipping planes
//		double abcd[4];
//		planes_[0]->get(abcd);
//		glm::vec3 loc10 = glm::vec3(abcd[0], abcd[1], abcd[2]);
//		float loc10w = (float)abcd[3];
//		planes_[1]->get(abcd);
//		glm::vec3 loc11 = glm::vec3(abcd[0], abcd[1], abcd[2]);
//		float loc11w = (float)abcd[3];
//		planes_[2]->get(abcd);
//		glm::vec3 loc12 = glm::vec3(abcd[0], abcd[1], abcd[2]);
//		float loc12w = (float)abcd[3];
//		planes_[3]->get(abcd);
//		glm::vec3 loc13 = glm::vec3(abcd[0], abcd[1], abcd[2]);
//		float loc13w = (float)abcd[3];
//		planes_[4]->get(abcd);
//		glm::vec3 loc14 = glm::vec3(abcd[0], abcd[1], abcd[2]);
//		float loc14w = (float)abcd[3];
//		planes_[5]->get(abcd);
//		glm::vec3 loc15 = glm::vec3(abcd[0], abcd[1], abcd[2]);
//		float loc15w = (float)abcd[3];
//
//		////////////////////////////////////////////////////////
//		// render bricks
//		// Set up transform
//		Transform *tform = tex_->transform();
//		double mvmat[16];
//		tform->get_trans(mvmat);
//		m_mv_mat2 = glm::mat4(
//			mvmat[0], mvmat[4], mvmat[8], mvmat[12],
//			mvmat[1], mvmat[5], mvmat[9], mvmat[13],
//			mvmat[2], mvmat[6], mvmat[10], mvmat[14],
//			mvmat[3], mvmat[7], mvmat[11], mvmat[15]);
//		m_mv_mat2 = m_mv_mat * m_mv_mat2;
//		glm::mat4 matrix0 = m_mv_mat2;
//		glm::mat4 matrix1 = m_proj_mat;
//		glm::mat4 matrix3 = glm::inverse(m_mv_mat2);
//
//		//bind 2d mask texture
//		bind_2d_mask();
//		glActiveTexture(GL_TEXTURE6);
//		int mask_w, mask_h;
//		glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &mask_w);
//		glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &mask_h);
//		size_t texsize = (size_t)mask_w*(size_t)mask_h*4;
//		unsigned char *mask2d = new unsigned char[texsize];
//		glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, mask2d);
//
//		//bind 2d weight map (unused)
//		if (use_2d)
//		{
//			bind_2d_weight();
//			glActiveTexture(GL_TEXTURE4);
//			//to do
//			glActiveTexture(GL_TEXTURE5);
//			//to do
//		}
//
//		bool clear_stroke = (paint_mode == 1 || paint_mode == 2 || (paint_mode == 4 && type == 0) || paint_mode == 6 || paint_mode == 7);
//
//		if (!tex_->isBrxml())
//		{
//			void *sdata = source->data;
//			int nb = tex_->nb(0);
//			unsigned char *tdata = (unsigned char *)target->data;
//			size_t nx = tex_->nx();
//			size_t ny = tex_->ny();
//			size_t nz = tex_->nz();
//			size_t vxnum = nx*ny*nz;
//			glm::mat4 matrix = matrix1 * matrix0;
//
//			#pragma omp parallel
//			{
//				#pragma omp for
//				for (long long i = 0; i < vxnum; i++)
//				{
//					size_t tx = i % nx;
//					size_t ty = (i / nx) % ny;
//					size_t tz = i / (nx * ny);
//
//					glm::vec4 t = glm::vec4( (float)tx/(float)nx, (float)ty/(float)ny, (float)tz/(float)nz, 1.0);
//					glm::vec4 s = matrix * t;
//					s = s / s.w;
//					s.x = s.x / 2.0 + 0.5;
//					s.y = s.y / 2.0 + 0.5;
//
//					size_t mask_x = (size_t)(s.x*mask_w);
//					size_t mask_y = (size_t)(s.y*mask_h);
//					if (mask_x < 0 || mask_x >= mask_w || mask_y < 0 || mask_y >= mask_h)
//						continue;
//
//					unsigned char mask_val = mask2d[ (mask_y*mask_w + mask_x)*4 ];
//					if (mask_val <= 242)//255*0.95
//						continue;
//
//					glm::vec3 t3 = glm::vec3(t);
//					if (glm::dot(t3, loc10) + loc10w < 0.0 ||
//						glm::dot(t3, loc11) + loc11w < 0.0 ||
//						glm::dot(t3, loc12) + loc12w < 0.0 ||
//						glm::dot(t3, loc13) + loc13w < 0.0 ||
//						glm::dot(t3, loc14) + loc14w < 0.0 ||
//						glm::dot(t3, loc15) + loc15w < 0.0)
//						continue;
//
//					float v = 0.0f;
//					if (nb == 1) v = ((unsigned char *)sdata)[i] / 255.0f;
//					else if (nb == 2) v = ((unsigned short *)sdata)[i] / 65535.0f;
//					v = loc2.x < 0.0 ? (1.0 + v * loc2.x) : v * loc2.x;
//
//					//SEG_BODY_INIT_BLEND_APPEND
//					float ret = v > 0.0 ? (v > loc7.x ? 1.0 : 0.0) : 0.0;
//					tdata[i] = (unsigned char)(ret*255.0f);
//					//StrokeColor = vec4(1.0);
//				}
//
//			}
//		}
//		else
//		{
//			int nb = tex_->nb(0);
//			unsigned char *tdata = (unsigned char *)target->data;
//			size_t nx = tex_->nx();
//			size_t ny = tex_->ny();
//			size_t nz = tex_->nz();
//			size_t vxnum = nx*ny*nz;
//			glm::mat4 matrix = matrix1 * matrix0;
//			unsigned int bnum = bricks->size();
//
//			//#pragma omp parallel for
//			for (int bid = 0; bid < bnum; bid++)
//			{
//				TextureBrick* b = (*bricks)[bid];
//				size_t box = b->ox();
//				size_t boy = b->oy();
//				size_t boz = b->oz();
//				size_t bnx = b->nx();
//				size_t bny = b->ny();
//				size_t bnz = b->nz();
//				size_t bvxnum = bnx * bny*bnz;
//				const void *sdata = b->getBrickData();
//
//				#pragma omp parallel for
//				for (long long i = 0; i < bvxnum; i++)
//				{
//					if (sdata)
//					{
//						size_t tx = i % bnx + box;
//						size_t ty = (i / bnx) % bny + boy;
//						size_t tz = i / (bnx * bny) + boz;
//
//						size_t tarvxid = tz * nx*ny + ty * nx + tx;
//
//						glm::vec4 t = glm::vec4((float)tx / (float)nx, (float)ty / (float)ny, (float)tz / (float)nz, 1.0);
//						glm::vec4 s = matrix * t;
//						s = s / s.w;
//						s.x = s.x / 2.0 + 0.5;
//						s.y = s.y / 2.0 + 0.5;
//
//						size_t mask_x = (size_t)(s.x*mask_w);
//						size_t mask_y = (size_t)(s.y*mask_h);
//						if (mask_x < 0 || mask_x >= mask_w || mask_y < 0 || mask_y >= mask_h)
//							continue;
//
//						unsigned char mask_val = mask2d[(mask_y*mask_w + mask_x) * 4];
//						if (mask_val <= 242)//255*0.95
//							continue;
//
//						glm::vec3 t3 = glm::vec3(t);
//						if (glm::dot(t3, loc10) + loc10w < 0.0 ||
//							glm::dot(t3, loc11) + loc11w < 0.0 ||
//							glm::dot(t3, loc12) + loc12w < 0.0 ||
//							glm::dot(t3, loc13) + loc13w < 0.0 ||
//							glm::dot(t3, loc14) + loc14w < 0.0 ||
//							glm::dot(t3, loc15) + loc15w < 0.0)
//							continue;
//
//						float v = 0.0f;
//						if (nb == 1) v = ((const unsigned char *)sdata)[i] / 255.0f;
//						else if (nb == 2) v = ((const unsigned short *)sdata)[i] / 65535.0f;
//						v = loc2.x < 0.0 ? (1.0 + v * loc2.x) : v * loc2.x;
//
//						//SEG_BODY_INIT_BLEND_APPEND
//						float ret = v > 0.0 ? (v > loc7.x ? 1.0 : 0.0) : 0.0;
//						tdata[tarvxid] = (unsigned char)(ret*255.0f);
//						//StrokeColor = vec4(1.0);
//					}
//				}
//			}
//
//		}
//	}
//	
//	wxString readCLcode(wxString filename)
//	{
//		wxString code = "";
//		if (wxFileExists(filename))
//		{
//			wxFileInputStream input(filename);
//			wxTextInputStream cl_file(input);
//			if (input.IsOk())
//			{
//				while (!input.Eof())
//				{
//					code += cl_file.ReadLine();
//					code += "\n";
//				}
//			}
//		}
//		return code;
//	}
//
//	void VolumeRenderer::draw_mask_th(float thresh, bool orthographic_p)
//	{
//		if (!tex_ || tex_->nstroke() < 0) return;
//
//		Ray view_ray = compute_view();
//
//		vector<TextureBrick*> *bricks = tex_->get_sorted_bricks(view_ray, orthographic_p);
//		if (!bricks || bricks->size() == 0)
//			return;
//
//		if (!m_dslt_kernel)
//		{
//            std::wstring exePath = wxStandardPaths::Get().GetExecutablePath().ToStdWstring();
//            exePath = exePath.substr(0,exePath.find_last_of(std::wstring()+GETSLASH()));
//            std::wstring pref = exePath + GETSLASH() + L"CL_code" + GETSLASH();
//
//			wxString code = "";
//			wxString filepath;
//			
//			filepath = pref + wxString("dslt2.cl");
//			code = readCLcode(filepath);
//			m_dslt_kernel = vol_kernel_factory_.kernel(code.ToStdString());
//			if (!m_dslt_kernel)
//				return;
//		}
//
//		string kn_th  = "threshold_mask";
//		if (!m_dslt_kernel->valid())
//		{
//			string kn_max = "dslt_max";
//			string kn_l2  = "dslt_l2";
//			string kn_b   = "dslt_binarize";
//			string kn_em  = "dslt_elem_min";
//			string kn_ap  = "dslt_ap_mask";
//			if (!m_dslt_kernel->create(kn_max))
//				return;
//			if (!m_dslt_kernel->create(kn_l2))
//				return;
//			if (!m_dslt_kernel->create(kn_b))
//				return;
//			if (!m_dslt_kernel->create(kn_em))
//				return;
//			if (!m_dslt_kernel->create(kn_ap))
//				return;
//			if (!m_dslt_kernel->create(kn_th))
//				return;
//		}
//
//		for (unsigned int i=0; i < bricks->size(); i++)
//		{
//			TextureBrick* b = (*bricks)[i];
//			int c = b->nmask();
//			int nb = b->nb(c);
//			float bmax = (b->nb(0) == 2) ? 65535.0 : 255.0;
//			float thval = thresh / bmax;
//			int brick_x = b->nx();
//			int brick_y = b->ny();
//			int brick_z = b->nz();
//
//			int ypitch = brick_x;
//			int zpitch = brick_x*brick_y;
//			size_t global_size[3] = { (size_t)brick_x, (size_t)brick_y, (size_t)brick_z };
//			size_t *local_size = NULL;
//			
//			GLint vd_id = load_brick(0, 0, bricks, i, GL_NEAREST, compression_);
//			GLint mask_id = load_brick_mask(bricks, i, GL_NEAREST);
//			GLint stroke_tex_id = load_brick_stroke(bricks, i, GL_NEAREST);
//
//			if (!glIsTexture(stroke_tex_id) || !glIsTexture(mask_id) || !glIsTexture(vd_id))
//				continue;
//			
//			m_dslt_kernel->setKernelArgTex3D(0, CL_MEM_READ_ONLY, vd_id, kn_th);
//			m_dslt_kernel->setKernelArgTex3D(1, CL_MEM_READ_ONLY, stroke_tex_id, kn_th);
//			m_dslt_kernel->setKernelArgTex3D(2, CL_MEM_WRITE_ONLY, mask_id, kn_th);
//			m_dslt_kernel->setKernelArgConst(3, sizeof(float), (void*)(&thval), kn_th);
//			m_dslt_kernel->setKernelArgConst(4, sizeof(int), (void*)(&ypitch), kn_th);
//			m_dslt_kernel->setKernelArgConst(5, sizeof(int), (void*)(&zpitch), kn_th);
//			m_dslt_kernel->execute(3, global_size, local_size, kn_th);
//
//			m_dslt_kernel->delTex(vd_id);
//			m_dslt_kernel->delTex(stroke_tex_id);
//			m_dslt_kernel->delTex(mask_id);
//
//			b->set_dirty(b->nmask(), true);
//		}
//
//		// Reset the blend functions after MIP
//		glBlendEquation(GL_FUNC_ADD);
//		glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
//		glDisable(GL_BLEND);
//
//		//enable depth test
//		glEnable(GL_DEPTH_TEST);
//	}
//	
//	//type: 0-initial; 1-diffusion-based growing; 2-masked filtering
//	//paint_mode: 1-select; 2-append; 3-erase; 4-diffuse; 5-flood; 6-clear; 7-all;
//	//			  11-posterize
//	//hr_mode (hidden removal): 0-none; 1-ortho; 2-persp
//	void VolumeRenderer::draw_mask_dslt(int type, int paint_mode, int hr_mode,
//		double ini_thresh, double gm_falloff, double scl_falloff,
//		double scl_translate, double w2d, double bins, bool orthographic_p,
//		bool estimate, int dslt_r, int dslt_q, double dslt_c)
//	{
//		if (estimate && type==0)
//			est_thresh_ = 0.0;
//		bool use_2d = glIsTexture(tex_2d_weight1_)&&
//			glIsTexture(tex_2d_weight2_)?true:false;
//
//		bool use_stroke = (tex_->nstroke() >= 0) ? true : false;
//
//		Ray view_ray = compute_view();
//
//		vector<TextureBrick*> *bricks = tex_->get_sorted_bricks(view_ray, orthographic_p);
//		if (!bricks || bricks->size() == 0)
//			return;
//
//		//mask frame buffer object
//		if (!glIsFramebuffer(fbo_mask_))
//			glGenFramebuffers(1, &fbo_mask_);
//		GLint cur_framebuffer_id;
//		glGetIntegerv(GL_FRAMEBUFFER_BINDING, &cur_framebuffer_id);
//		glBindFramebuffer(GL_FRAMEBUFFER, fbo_mask_);
//
//		//--------------------------------------------------------------------------
//		// Set up shaders
//		//seg shader
//		ShaderProgram* seg_shader = 0;
//
//		seg_shader = seg_shader_factory_.shader(
//						SEG_SHDR_INITIALIZE, 2, hr_mode,
//						use_2d, true, depth_peel_, true, hiqual_, use_stroke);
//
//		if (seg_shader)
//		{
//			if (!seg_shader->valid())
//				seg_shader->create();
//			seg_shader->bind();
//		}
//
//		//set uniforms
//		//set up shading
//		Vector light = compute_view().direction();
//		light.safe_normalize();
//		seg_shader->setLocalParam(0, light.x(), light.y(), light.z(), alpha_);
//		if (shading_)
//			seg_shader->setLocalParam(1, 2.0 - ambient_, diffuse_, specular_, shine_);
//		else
//			seg_shader->setLocalParam(1, 2.0 - ambient_, 0.0, specular_, shine_);
//
//		//spacings
//		double spcx, spcy, spcz;
//		tex_->get_spacings(spcx, spcy, spcz);
//		seg_shader->setLocalParam(5, spcx, spcy, spcz, 1.0);
//
//		//transfer function
//		seg_shader->setLocalParam(2, inv_?-scalar_scale_:scalar_scale_, gm_scale_, lo_thresh_, hi_thresh_);
//		seg_shader->setLocalParam(3, 1.0/gamma3d_, gm_thresh_, offset_, sw_);
//		seg_shader->setLocalParam(6, color_.r(), color_.g(), color_.b(), 0.0);
//
//		//setup depth peeling
//		//if (depth_peel_)
//		//	seg_shader->setLocalParam(7, 1.0/double(w2), 1.0/double(h2), 0.0, 0.0);
//
//		//thresh1
//		seg_shader->setLocalParam(7, 0.0, gm_falloff, scl_falloff, 0.0);
//		//w2d
//		seg_shader->setLocalParam(8, w2d, bins, 0.0, 0.0);
//
//		//set clipping planes
//		double abcd[4];
//		planes_[0]->get(abcd);
//		seg_shader->setLocalParam(10, abcd[0], abcd[1], abcd[2], abcd[3]);
//		planes_[1]->get(abcd);
//		seg_shader->setLocalParam(11, abcd[0], abcd[1], abcd[2], abcd[3]);
//		planes_[2]->get(abcd);
//		seg_shader->setLocalParam(12, abcd[0], abcd[1], abcd[2], abcd[3]);
//		planes_[3]->get(abcd);
//		seg_shader->setLocalParam(13, abcd[0], abcd[1], abcd[2], abcd[3]);
//		planes_[4]->get(abcd);
//		seg_shader->setLocalParam(14, abcd[0], abcd[1], abcd[2], abcd[3]);
//		planes_[5]->get(abcd);
//		seg_shader->setLocalParam(15, abcd[0], abcd[1], abcd[2], abcd[3]);
//
//		////////////////////////////////////////////////////////
//		// render bricks
//		// Set up transform
//		Transform *tform = tex_->transform();
//		double mvmat[16];
//		tform->get_trans(mvmat);
//		m_mv_mat2 = glm::mat4(
//			mvmat[0], mvmat[4], mvmat[8], mvmat[12],
//			mvmat[1], mvmat[5], mvmat[9], mvmat[13],
//			mvmat[2], mvmat[6], mvmat[10], mvmat[14],
//			mvmat[3], mvmat[7], mvmat[11], mvmat[15]);
//		m_mv_mat2 = m_mv_mat * m_mv_mat2;
//		seg_shader->setLocalParamMatrix(0, glm::value_ptr(m_mv_mat2));
//		seg_shader->setLocalParamMatrix(1, glm::value_ptr(m_proj_mat));
//		if (hr_mode > 0)
//		{
//			glm::mat4 mv_inv = glm::inverse(m_mv_mat2);
//			seg_shader->setLocalParamMatrix(3, glm::value_ptr(mv_inv));
//		}
//
//		//bind 2d mask texture
//		bind_2d_mask();
//		//bind 2d weight map
//		if (use_2d) bind_2d_weight();
//
//		glDisable(GL_DEPTH_TEST);
//		glDisable(GL_BLEND);
//
//		GLint vp[4];
//		glGetIntegerv(GL_VIEWPORT, vp);
//
//
//
//		////////////////////////////////
//		if(dslt_q < 1) return;
//		if(dslt_r < 1) return;
//
//		if (!m_dslt_kernel)
//		{
//            std::wstring exePath = wxStandardPaths::Get().GetExecutablePath().ToStdWstring();
//            exePath = exePath.substr(0,exePath.find_last_of(std::wstring()+GETSLASH()));
//            std::wstring pref = exePath + GETSLASH() + L"CL_code" + GETSLASH();
//
//			wxString code = "";
//			wxString filepath;
//			
//			filepath = pref + wxString("dslt2.cl");
//			code = readCLcode(filepath);
//			m_dslt_kernel = vol_kernel_factory_.kernel(code.ToStdString());
//			if (!m_dslt_kernel)
//				return;
//		}
//
//		string kn_max = "dslt_max";
//		string kn_l2  = "dslt_l2";
//		string kn_b   = "dslt_binarize";
//		string kn_em  = "dslt_elem_min";
//		string kn_ap  = "dslt_ap_mask";
//		string kn_th  = "threshold_mask";
//		if (!m_dslt_kernel->valid())
//		{
//			if (!m_dslt_kernel->create(kn_max))
//				return;
//			if (!m_dslt_kernel->create(kn_l2))
//				return;
//			if (!m_dslt_kernel->create(kn_b))
//				return;
//			if (!m_dslt_kernel->create(kn_em))
//				return;
//			if (!m_dslt_kernel->create(kn_ap))
//				return;
//			if (!m_dslt_kernel->create(kn_th))
//				return;
//		}
//
//		int rd = dslt_q;
//	    double a_interval = (PI / 2.0) / (double)rd;
//		double *slatitable  = new double [rd*2*(rd*2-1)+1];
//		double *clatitable  = new double [rd*2*(rd*2-1)+1];
//		double *slongitable = new double [rd*2*(rd*2-1)+1];
//		double *clongitable = new double [rd*2*(rd*2-1)+1];
//		double *sintable = new double [rd*2];
//		double *costable = new double [rd*2];
//		int knum = rd*2*(rd*2-1)+1;
//
//		int sc_tablesize = (rd*2*(rd*2-1)+1)*4;
//		float *sctptr = new float [sc_tablesize];
//
//		slatitable[0] = 0.0; clatitable[0] = 1.0; slongitable[0] = 0.0; clongitable[0] = 0.0;
//		for(int b = 0; b < rd*2; b++){
//			for(int a = 1; a < rd*2; a++){
//				int id = b*(rd*2-1) + (a-1) + 1;
//				slatitable[id] = sin(a_interval*a);
//				clatitable[id] = cos(a_interval*a);
//				slongitable[id] = sin(a_interval*b);
//				clongitable[id] = cos(a_interval*b);
//
//				sctptr[4*id]   = (float)slatitable[id];
//				sctptr[4*id+1] = (float)clatitable[id];
//				sctptr[4*id+2] = (float)slongitable[id];
//				sctptr[4*id+3] = (float)clongitable[id];
//			}
//		}
//		
//		for(int a = 0; a < rd*2; a++){
//			sintable[a] = sin(a_interval*a);
//			costable[a] = cos(a_interval*a);
//		}
//
//		Nrrd *mask = tex_->get_nrrd(tex_->nmask());
//		if (!mask || !mask->data) return;
//
//		////////////////////
//
//
//
//		float matrix[16];
//		for (unsigned int i=0; i < bricks->size(); i++)
//		{
//			TextureBrick* b = (*bricks)[i];
//			b->set_dirty(b->nmask(), true);
//
//			BBox bbox = b->bbox();
//			matrix[0] = float(bbox.max().x()-bbox.min().x());
//			matrix[1] = 0.0f;
//			matrix[2] = 0.0f;
//			matrix[3] = 0.0f;
//			matrix[4] = 0.0f;
//			matrix[5] = float(bbox.max().y()-bbox.min().y());
//			matrix[6] = 0.0f;
//			matrix[7] = 0.0f;
//			matrix[8] = 0.0f;
//			matrix[9] = 0.0f;
//			matrix[10] = float(bbox.max().z()-bbox.min().z());
//			matrix[11] = 0.0f;
//			matrix[12] = float(bbox.min().x());
//			matrix[13] = float(bbox.min().y());
//			matrix[14] = float(bbox.min().z());
//			matrix[15] = 1.0f;
//			seg_shader->setLocalParamMatrix(2, matrix);
//
//			//set viewport size the same as the texture
//			glViewport(0, 0, b->nx(), b->ny());
//
//			//load the texture
//			GLint tex_id = -1;
//			GLint vd_id = load_brick(0, 0, bricks, i, GL_NEAREST, compression_);
//
//			//generate temporary mask texture
//			int c = b->nmask();
//			int nb = b->nb(c);
//			int nx = b->nx();
//			int ny = b->ny();
//			int nz = b->nz();
//			GLenum textype = b->tex_type(c);
//
//			double bmax = (b->nb(0) == 2) ? 65535.0 : 255.0;
//			float cf = (float)(dslt_c / bmax);
//
//			GLuint temp_tex;
//			unsigned char *tmp_data = new unsigned char[nx*ny*nz*nb];
//			memset(tmp_data, 0, nx*ny*nz*nb);
//			
//			glActiveTexture(GL_TEXTURE0+c);
//			glGenTextures(1, &temp_tex);
//			glEnable(GL_TEXTURE_3D);
//			glBindTexture(GL_TEXTURE_3D, temp_tex);
//			glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
//			glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
//			glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
//			glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
//			glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
//#ifdef _WIN32
//			glPixelStorei(GL_UNPACK_ROW_LENGTH, nx);
//			glPixelStorei(GL_UNPACK_IMAGE_HEIGHT, ny);
//#else
//			glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
//			glPixelStorei(GL_UNPACK_IMAGE_HEIGHT, 0);
//#endif
//			glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
//
//			GLint internal_format = GL_R8;
//			GLenum format = (nb == 1 ? GL_RED : GL_RGBA);
//			if (ShaderProgram::shaders_supported())
//			{
//				if (glTexImage3D)
//				{
//					glTexImage3D(GL_TEXTURE_3D, 0, internal_format, nx, ny, nz, 0, format, b->tex_type(c), 0);
//					glTexSubImage3D(GL_TEXTURE_3D, 0, 0, 0, 0, nx, ny, nz, format, b->tex_type(c), tmp_data);
//				}
//			}
//#ifdef _WIN32
//			glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
//			glPixelStorei(GL_UNPACK_IMAGE_HEIGHT, 0);
//#endif
//			glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
//			glActiveTexture(GL_TEXTURE0);
//
//			GLint mask_id = load_brick_mask(bricks, i, GL_NEAREST);
//			glActiveTexture(GL_TEXTURE0+c);
//			glBindTexture(GL_TEXTURE_3D, mask_id);
//			glPixelStorei(GL_PACK_ROW_LENGTH, nx);
//			glPixelStorei(GL_PACK_IMAGE_HEIGHT, ny);
//			glPixelStorei(GL_PACK_ALIGNMENT, 1);
//			glGetTexImage(GL_TEXTURE_3D, 0, format, b->tex_type(c), tmp_data);
//			glPixelStorei(GL_PACK_ROW_LENGTH, 0);
//			glPixelStorei(GL_PACK_IMAGE_HEIGHT, 0);
//			glPixelStorei(GL_PACK_ALIGNMENT, 4);
//			glBindTexture(GL_TEXTURE_3D, 0);
//			glActiveTexture(GL_TEXTURE0);
//			
//			//size and sample rate
//			seg_shader->setLocalParam(4, 1.0/b->nx(), 1.0/b->ny(), 1.0/b->nz(),
//				mode_==MODE_OVER?1.0/sampling_rate_:1.0);
//
//			//draw each slice
//			for (int z=0; z<b->nz(); z++)
//			{
//				glFramebufferTexture3D(GL_FRAMEBUFFER, 
//					GL_COLOR_ATTACHMENT0,
//					GL_TEXTURE_3D,
//					temp_tex,
//					0,
//					z);
//
//				draw_view_quad(double(z+0.5) / double(b->nz()));
//			}
//
//			glFinish();
///*			
//			glActiveTexture(GL_TEXTURE0+c);
//			glBindTexture(GL_TEXTURE_3D, temp_tex);
//			glPixelStorei(GL_PACK_ROW_LENGTH, nx);
//			glPixelStorei(GL_PACK_IMAGE_HEIGHT, ny);
//			glPixelStorei(GL_PACK_ALIGNMENT, 1);
//			glGetTexImage(GL_TEXTURE_3D, 0, format, b->tex_type(c), tmp_data);
//			glPixelStorei(GL_PACK_ROW_LENGTH, 0);
//			glPixelStorei(GL_PACK_IMAGE_HEIGHT, 0);
//			glPixelStorei(GL_PACK_ALIGNMENT, 4);
//			glBindTexture(GL_TEXTURE_3D, 0);
//			glActiveTexture(GL_TEXTURE0);
//			glDeleteTextures(1, (GLuint*)&temp_tex);
//
//			long long mask_offset = (long long)(b->oz()) * (long long)(b->sx()) * (long long)(b->sy()) +
//									(long long)(b->oy()) * (long long)(b->sx()) +
//									(long long)(b->ox());
//			uint8 *mask_offset_pointer = (uint8 *)mask->data + mask_offset;
//			uint8* dst_p = mask_offset_pointer; 
//			uint8* src_p = tmp_data;
//			int ypitch = nx;
//			int zpitch = nx*ny;
//			int mask_ypitch = b->sx();
//			int mask_zpitch = b->sx()*b->sy();
//			for (int z = 0; z < nz; z++)
//			{
//				uint8* z_st_dst_p = dst_p;
//				for (int y = 0; y < ny; y++)
//				{
//					memcpy(dst_p, src_p, nx*sizeof(uint8));
//					dst_p += mask_ypitch;
//					src_p += nx;
//				}
//				dst_p = z_st_dst_p + mask_zpitch;
//			}
//*/
//			
//			///////////////////////////dslt_line
//			{
//				GLint data_id = load_brick(0, 0, bricks, i);
//				glActiveTexture(GL_TEXTURE0+c);
//				glBindTexture(GL_TEXTURE_3D, temp_tex);
//				glActiveTexture(GL_TEXTURE0);
//				unsigned char *mask_data = tmp_data;
//				int brick_x = b->nx();
//				int brick_y = b->ny();
//				int brick_z = b->nz();
//				int bsize = brick_x*brick_y*brick_z;
//				float* out_cl_buf = new float[bsize];
//				std::fill(out_cl_buf, out_cl_buf+bsize, 0.0f);
//				float* out_cl_final_buf = new float[bsize];
//				std::fill(out_cl_final_buf, out_cl_final_buf+bsize, FLT_MAX);
//				int* n_cl_buf = new int[bsize];
//				uint8* mask_temp_buf = new uint8[bsize];
//				int ypitch = brick_x;
//				int zpitch = brick_x*brick_y;
//
//				long long mask_offset = (long long)(b->oz()) * (long long)(b->sx()) * (long long)(b->sy()) +
//										(long long)(b->oy()) * (long long)(b->sx()) +
//										(long long)(b->ox());
//				uint8 *mask_offset_pointer = (uint8 *)mask->data + mask_offset;
//				int mask_ypitch = b->sx();
//				int mask_zpitch = b->sx()*b->sy();
//
//				size_t global_size[3] = { (size_t)brick_x, (size_t)brick_y, (size_t)brick_z };
//				//size_t local_size[3] = { 1, 1, 1 }; //too slow
//				size_t *local_size = NULL;
//				float pattern_zero = 0.0f;
//				float pattern_max = FLT_MAX;
//
//				int r = dslt_r;
//				do{
//
//					int ksize = 2*r + 1;
//					double *filter = new double[ksize];
//					float *cl_filter = new float[ksize];
//					{
//						double sigma = 0.3*(ksize/2 - 1) + 0.8;
//						double denominator = 2.0*sigma*sigma;
//						double sum;
//						double xx, d;
//						int x;
//
//						sum = 0.0;
//						for(x = 0; x < ksize; x++){
//							xx = x - (ksize - 1)/2;
//							d = xx*xx;
//							filter[x] = exp(-1.0*d/denominator);
//							sum += filter[x];
//						}
//						for(x = 0; x < ksize; x++) cl_filter[x] = filter[x] / sum;
//					}
//					delete[] filter;
//					m_dslt_kernel->setKernelArgTex3D(0, CL_MEM_READ_ONLY, data_id, kn_max);
//					m_dslt_kernel->setKernelArgTex3D(1, CL_MEM_READ_ONLY, temp_tex, kn_max);
//					m_dslt_kernel->setKernelArgBuf(2, CL_MEM_READ_WRITE | CL_MEM_COPY_HOST_PTR, bsize*sizeof(float), out_cl_buf, kn_max);
//					m_dslt_kernel->setKernelArgBuf(3, CL_MEM_READ_WRITE | CL_MEM_COPY_HOST_PTR, bsize*sizeof(int), n_cl_buf, kn_max);
//					m_dslt_kernel->setKernelArgBuf(4, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR, ksize*sizeof(float), cl_filter, kn_max);
//					m_dslt_kernel->setKernelArgConst(5, sizeof(int), (void*)(&r), kn_max);
//					m_dslt_kernel->setKernelArgConst(10, sizeof(int), (void*)(&ypitch), kn_max);
//					m_dslt_kernel->setKernelArgConst(11, sizeof(int), (void*)(&zpitch), kn_max);
//					m_dslt_kernel->writeBuffer(out_cl_buf, &pattern_zero, sizeof(float), 0, bsize*sizeof(float));
//
//					for(int n = 0; n < knum; n++){
//						float drx = (float)(slatitable[n]*clongitable[n]);
//						float dry = (float)(slatitable[n]*slongitable[n]);
//						float drz = (float)clatitable[n];
//
//						m_dslt_kernel->setKernelArgConst(6, sizeof(int), (void*)(&n), kn_max);
//						m_dslt_kernel->setKernelArgConst(7, sizeof(float), (void*)(&drx), kn_max);
//						m_dslt_kernel->setKernelArgConst(8, sizeof(float), (void*)(&dry), kn_max);
//						m_dslt_kernel->setKernelArgConst(9, sizeof(float), (void*)(&drz), kn_max);
//						m_dslt_kernel->execute(3, global_size, local_size, kn_max);
//					}
//
//					int l2knum = rd*2;
//					m_dslt_kernel->setKernelArgTex3D(0, CL_MEM_READ_ONLY, data_id, kn_l2);
//					m_dslt_kernel->setKernelArgTex3D(1, CL_MEM_READ_ONLY, temp_tex, kn_l2);
//					m_dslt_kernel->setKernelArgBuf(2, CL_MEM_READ_WRITE | CL_MEM_COPY_HOST_PTR, bsize*sizeof(float), out_cl_buf, kn_l2);
//					m_dslt_kernel->setKernelArgBuf(3, CL_MEM_READ_WRITE | CL_MEM_COPY_HOST_PTR, bsize*sizeof(int), n_cl_buf, kn_l2);
//					m_dslt_kernel->setKernelArgBuf(4, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR, ksize*sizeof(float), cl_filter, kn_l2);
//					m_dslt_kernel->setKernelArgBuf(5, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR, sc_tablesize*sizeof(float), sctptr, kn_l2);
//					m_dslt_kernel->setKernelArgConst(6, sizeof(int), (void*)(&r), kn_l2);
//					m_dslt_kernel->setKernelArgConst(7, sizeof(int), (void*)(&l2knum), kn_l2);
//					m_dslt_kernel->setKernelArgConst(10, sizeof(int), (void*)(&ypitch), kn_l2);
//					m_dslt_kernel->setKernelArgConst(11, sizeof(int), (void*)(&zpitch), kn_l2);
//					m_dslt_kernel->writeBuffer(out_cl_buf, &pattern_zero, sizeof(float), 0, bsize*sizeof(float));
//
//					for(int a = 0; a < rd*2; a++){
//						float bx = (float)costable[a];
//						float by = (float)sintable[a];
//
//						m_dslt_kernel->setKernelArgConst(8, sizeof(float), (void*)(&bx), kn_l2);
//						m_dslt_kernel->setKernelArgConst(9, sizeof(float), (void*)(&by), kn_l2);
//						m_dslt_kernel->execute(3, global_size, local_size, kn_l2);
//					}
//
//					m_dslt_kernel->setKernelArgBuf(0, CL_MEM_READ_WRITE | CL_MEM_COPY_HOST_PTR, bsize*sizeof(float), out_cl_buf, kn_em);
//					m_dslt_kernel->setKernelArgBuf(1, CL_MEM_READ_WRITE | CL_MEM_COPY_HOST_PTR, bsize*sizeof(float), out_cl_final_buf, kn_em);
//					m_dslt_kernel->setKernelArgTex3D(2, CL_MEM_READ_ONLY, temp_tex, kn_em);
//					m_dslt_kernel->setKernelArgConst(3, sizeof(int), (void*)(&ypitch), kn_em);
//					m_dslt_kernel->setKernelArgConst(4, sizeof(int), (void*)(&zpitch), kn_em);
//					m_dslt_kernel->execute(3, global_size, local_size, kn_em);
//
//					m_dslt_kernel->delBuf(cl_filter);
//					delete[] cl_filter;
//					r /= 2;
//				} while (r >= 3);
//
//				m_dslt_kernel->readBuffer(out_cl_final_buf);
//
//				m_dslt_kernel->setKernelArgTex3D(0, CL_MEM_READ_ONLY, data_id, kn_b);
//				m_dslt_kernel->setKernelArgTex3D(1, CL_MEM_READ_ONLY, temp_tex, kn_b);
//				m_dslt_kernel->setKernelArgBuf(2, CL_MEM_READ_WRITE | CL_MEM_COPY_HOST_PTR, bsize*sizeof(float), out_cl_final_buf, kn_b);
//				m_dslt_kernel->setKernelArgBuf(3, CL_MEM_READ_WRITE | CL_MEM_COPY_HOST_PTR, bsize*sizeof(uint8), mask_temp_buf, kn_b);
//				m_dslt_kernel->setKernelArgConst(4, sizeof(float), (void*)(&cf), kn_b);
//				m_dslt_kernel->setKernelArgConst(5, sizeof(int), (void*)(&ypitch), kn_b);
//				m_dslt_kernel->setKernelArgConst(6, sizeof(int), (void*)(&zpitch), kn_b);
//				m_dslt_kernel->execute(3, global_size, local_size, kn_b);
//
//				m_dslt_kernel->setKernelArgBuf(0, CL_MEM_READ_WRITE | CL_MEM_COPY_HOST_PTR, bsize*sizeof(uint8), mask_temp_buf, kn_ap);
//				m_dslt_kernel->setKernelArgTex3D(1, CL_MEM_READ_ONLY, temp_tex, kn_ap);
//				m_dslt_kernel->setKernelArgBuf(2, CL_MEM_READ_WRITE | CL_MEM_COPY_HOST_PTR, bsize*sizeof(uint8), mask_data, kn_ap);
//				m_dslt_kernel->setKernelArgConst(3, sizeof(int), (void*)(&ypitch), kn_ap);
//				m_dslt_kernel->setKernelArgConst(4, sizeof(int), (void*)(&zpitch), kn_ap);
//				m_dslt_kernel->execute(3, global_size, local_size, kn_ap);
//
//				m_dslt_kernel->readBuffer(mask_data);
//				
//				if (bricks->size() == 1)
//					memcpy(mask->data, mask_data, bsize*sizeof(uint8));
//				else
//				{
//					uint8* dst_p = mask_offset_pointer; 
//					uint8* src_p = mask_data;
//					for (int z = 0; z < brick_z; z++)
//					{
//						uint8* z_st_dst_p = dst_p;
//						for (int y = 0; y < brick_y; y++)
//						{
//							memcpy(dst_p, src_p, brick_x*sizeof(uint8));
//							dst_p += mask_ypitch;
//							src_p += brick_x;
//						}
//						dst_p = z_st_dst_p + mask_zpitch;
//					}
//				}
//
//				delete[] out_cl_buf;
//				delete[] out_cl_final_buf;
//				delete[] n_cl_buf;
//				delete[] mask_temp_buf;
//
//				m_dslt_kernel->delBuf(out_cl_buf);
//				m_dslt_kernel->delBuf(n_cl_buf);
//				m_dslt_kernel->delBuf(sctptr);
//				m_dslt_kernel->delBuf(out_cl_final_buf);
//				m_dslt_kernel->delBuf(mask_temp_buf);
//				m_dslt_kernel->delBuf(mask_data);
//				m_dslt_kernel->delTex(data_id);
//				m_dslt_kernel->delTex(temp_tex);
//			}
//			/////////////////////////
//
//			glActiveTexture(GL_TEXTURE0+c);
//			glBindTexture(GL_TEXTURE_3D, 0);
//			glActiveTexture(GL_TEXTURE0);
//			if (glIsTexture(temp_tex))
//				glDeleteTextures(1, (GLuint*)&temp_tex);
//			delete[] tmp_data;
//		}
//
//		glViewport(vp[0], vp[1], vp[2], vp[3]);
//
//		//release 2d mask
//		release_texture(6, GL_TEXTURE_2D);
//		//release 2d weight map
//		if (use_2d)
//		{
//			release_texture(4, GL_TEXTURE_2D);
//			release_texture(5, GL_TEXTURE_2D);
//		}
//
//		//release 3d mask
//		release_texture((*bricks)[0]->nmask(), GL_TEXTURE_3D);
//
//		//unbind framebuffer
//		glBindFramebuffer(GL_FRAMEBUFFER, cur_framebuffer_id);
//
//		//release seg shader
//		if (seg_shader && seg_shader->valid())
//			seg_shader->release();
//
//		// Release texture
//		release_texture(0, GL_TEXTURE_3D);
//
//		// Reset the blend functions after MIP
//		glBlendEquation(GL_FUNC_ADD);
//		glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
//		glDisable(GL_BLEND);
//
//		//enable depth test
//		glEnable(GL_DEPTH_TEST);
//
//
//		delete[] slatitable;
//		delete[] clatitable;
//		delete[] slongitable;
//		delete[] clongitable;
//		delete[] sintable;
//		delete[] costable;
//		delete[] sctptr;
//
//		clear_tex_pool();
//	}
//
//	void VolumeRenderer::dslt_mask(int rmax, int quality, double c)
//	{
//		if(quality < 1) return;
//		if(rmax < 1) return;
//
//		float cf = (float)c;
//		
//		if (!m_dslt_kernel)
//		{
//#ifdef _WIN32
//			wxString pref = L".\\CL_code\\";
//#else
//			wxString pref = L"./CL_code/";
//#endif
//			wxString code = "";
//			wxString filepath;
//			
//			filepath = pref + wxString("dslt.cl");
//			code = readCLcode(filepath);
//			m_dslt_kernel = vol_kernel_factory_.kernel(code.ToStdString());
//			if (!m_dslt_kernel)
//				return;
//		}
//
//		string kn_max = "dslt_max";
//		string kn_l2  = "dslt_l2";
//		string kn_b   = "dslt_binarize";
//		string kn_em  = "dslt_elem_min";
//		if (!m_dslt_kernel->valid())
//		{
//			if (!m_dslt_kernel->create(kn_max))
//				return;
//			if (!m_dslt_kernel->create(kn_l2))
//				return;
//			if (!m_dslt_kernel->create(kn_b))
//				return;
//			if (!m_dslt_kernel->create(kn_em))
//				return;
//		}
//
//		int rd = quality;
//	    double a_interval = (PI / 2.0) / (double)rd;
//		double *slatitable  = new double [rd*2*(rd*2-1)+1];
//		double *clatitable  = new double [rd*2*(rd*2-1)+1];
//		double *slongitable = new double [rd*2*(rd*2-1)+1];
//		double *clongitable = new double [rd*2*(rd*2-1)+1];
//		double *sintable = new double [rd*2];
//		double *costable = new double [rd*2];
//		int knum = rd*2*(rd*2-1)+1;
//
//		int sc_tablesize = (rd*2*(rd*2-1)+1)*4;
//		float *sctptr = new float [sc_tablesize];
//
//		slatitable[0] = 0.0; clatitable[0] = 1.0; slongitable[0] = 0.0; clongitable[0] = 0.0;
//		for(int b = 0; b < rd*2; b++){
//			for(int a = 1; a < rd*2; a++){
//				int id = b*(rd*2-1) + (a-1) + 1;
//				slatitable[id] = sin(a_interval*a);
//				clatitable[id] = cos(a_interval*a);
//				slongitable[id] = sin(a_interval*b);
//				clongitable[id] = cos(a_interval*b);
//
//				sctptr[4*id]   = (float)slatitable[id];
//				sctptr[4*id+1] = (float)clatitable[id];
//				sctptr[4*id+2] = (float)slongitable[id];
//				sctptr[4*id+3] = (float)clongitable[id];
//			}
//		}
//		
//		for(int a = 0; a < rd*2; a++){
//			sintable[a] = sin(a_interval*a);
//			costable[a] = cos(a_interval*a);
//		}
//
//		//get bricks
//		Ray view_ray(Point(0.802, 0.267, 0.534), Vector(0.802, 0.267, 0.534));
//		tex_->set_sort_bricks();
//		vector<TextureBrick*> *bricks = tex_->get_sorted_bricks(view_ray);
//		if (!bricks || bricks->size() == 0)
//			return;
//
//		Nrrd *mask = tex_->get_nrrd(tex_->nmask());
//		if (!mask || !mask->data) return;
//
//		for (unsigned int i = 0; i<bricks->size(); ++i)
//		{
//			TextureBrick* b = (*bricks)[i];
//			b->set_dirty(b->nmask(), true);
//			GLint data_id = load_brick(0, 0, bricks, i);
//			GLint mask_data_id = load_brick_mask(bricks, i);
//			int brick_x = b->nx();
//			int brick_y = b->ny();
//			int brick_z = b->nz();
//			int bsize = brick_x*brick_y*brick_z;
//			float* out_cl_buf = new float[bsize];
//			std::fill(out_cl_buf, out_cl_buf+bsize, 0.0f);
//			float* out_cl_final_buf = new float[bsize];
//			std::fill(out_cl_final_buf, out_cl_final_buf+bsize, FLT_MAX);
//			int* n_cl_buf = new int[bsize];
//			uint8* mask_temp_buf = new uint8[bsize];
//			int ypitch = brick_x;
//			int zpitch = brick_x*brick_y;
//
//			long long mask_offset = (long long)(b->oz()) * (long long)(b->sx()) * (long long)(b->sy()) +
//									(long long)(b->oy()) * (long long)(b->sx()) +
//									(long long)(b->ox());
//			uint8 *mask_offset_pointer = (uint8 *)mask->data + mask_offset;
//			int mask_ypitch = b->sx();
//			int mask_zpitch = b->sx()*b->sy();
//
//			size_t global_size[3] = { (size_t)brick_x, (size_t)brick_y, (size_t)brick_z };
//			//size_t local_size[3] = { 1, 1, 1 }; //too slow
//			size_t *local_size = NULL;
//			float pattern_zero = 0.0f;
//			float pattern_max = FLT_MAX;
//
//			int r = rmax;
//			do{
//
//				int ksize = 2*r + 1;
//				double *filter = new double[ksize];
//				float *cl_filter = new float[ksize];
//				{
//					double sigma = 0.3*(ksize/2 - 1) + 0.8;
//					double denominator = 2.0*sigma*sigma;
//					double sum;
//					double xx, d;
//					int x;
//
//					sum = 0.0;
//					for(x = 0; x < ksize; x++){
//						xx = x - (ksize - 1)/2;
//						d = xx*xx;
//						filter[x] = exp(-1.0*d/denominator);
//						sum += filter[x];
//					}
//					for(x = 0; x < ksize; x++) cl_filter[x] = filter[x] / sum;
//				}
//				delete[] filter;
//				m_dslt_kernel->setKernelArgTex3D(0, CL_MEM_READ_ONLY, data_id, kn_max);
//				m_dslt_kernel->setKernelArgBuf(1, CL_MEM_READ_WRITE | CL_MEM_COPY_HOST_PTR, bsize*sizeof(float), out_cl_buf, kn_max);
//				m_dslt_kernel->setKernelArgBuf(2, CL_MEM_READ_WRITE | CL_MEM_COPY_HOST_PTR, bsize*sizeof(int), n_cl_buf, kn_max);
//				m_dslt_kernel->setKernelArgBuf(3, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR, ksize*sizeof(float), cl_filter, kn_max);
//				m_dslt_kernel->setKernelArgConst(4, sizeof(int), (void*)(&r), kn_max);
//				m_dslt_kernel->setKernelArgConst(9, sizeof(int), (void*)(&ypitch), kn_max);
//				m_dslt_kernel->setKernelArgConst(10, sizeof(int), (void*)(&zpitch), kn_max);
//				m_dslt_kernel->writeBuffer(out_cl_buf, &pattern_zero, sizeof(float), 0, bsize*sizeof(float));
//
//				for(int n = 0; n < knum; n++){
//					float drx = (float)(slatitable[n]*clongitable[n]);
//					float dry = (float)(slatitable[n]*slongitable[n]);
//					float drz = (float)clatitable[n];
//
//					m_dslt_kernel->setKernelArgConst(5, sizeof(int), (void*)(&n), kn_max);
//					m_dslt_kernel->setKernelArgConst(6, sizeof(float), (void*)(&drx), kn_max);
//					m_dslt_kernel->setKernelArgConst(7, sizeof(float), (void*)(&dry), kn_max);
//					m_dslt_kernel->setKernelArgConst(8, sizeof(float), (void*)(&drz), kn_max);
//					m_dslt_kernel->execute(3, global_size, local_size, kn_max);
//				}
//
//				int l2knum = rd*2;
//				m_dslt_kernel->setKernelArgTex3D(0, CL_MEM_READ_ONLY, data_id, kn_l2);
//				m_dslt_kernel->setKernelArgBuf(1, CL_MEM_READ_WRITE | CL_MEM_COPY_HOST_PTR, bsize*sizeof(float), out_cl_buf, kn_l2);
//				m_dslt_kernel->setKernelArgBuf(2, CL_MEM_READ_WRITE | CL_MEM_COPY_HOST_PTR, bsize*sizeof(int), n_cl_buf, kn_l2);
//				m_dslt_kernel->setKernelArgBuf(3, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR, ksize*sizeof(float), cl_filter, kn_l2);
//				m_dslt_kernel->setKernelArgBuf(4, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR, sc_tablesize*sizeof(float), sctptr, kn_l2);
//				m_dslt_kernel->setKernelArgConst(5, sizeof(int), (void*)(&r), kn_l2);
//				m_dslt_kernel->setKernelArgConst(6, sizeof(int), (void*)(&l2knum), kn_l2);
//				m_dslt_kernel->setKernelArgConst(9, sizeof(int), (void*)(&ypitch), kn_l2);
//				m_dslt_kernel->setKernelArgConst(10, sizeof(int), (void*)(&zpitch), kn_l2);
//				m_dslt_kernel->writeBuffer(out_cl_buf, &pattern_zero, sizeof(float), 0, bsize*sizeof(float));
//
//				for(int a = 0; a < rd*2; a++){
//					float bx = (float)costable[a];
//					float by = (float)sintable[a];
//										
//					m_dslt_kernel->setKernelArgConst(7, sizeof(float), (void*)(&bx), kn_l2);
//					m_dslt_kernel->setKernelArgConst(8, sizeof(float), (void*)(&by), kn_l2);
//					m_dslt_kernel->execute(3, global_size, local_size, kn_l2);
//				}
//				
//				m_dslt_kernel->setKernelArgBuf(0, CL_MEM_READ_WRITE | CL_MEM_COPY_HOST_PTR, bsize*sizeof(float), out_cl_buf, kn_em);
//				m_dslt_kernel->setKernelArgBuf(1, CL_MEM_READ_WRITE | CL_MEM_COPY_HOST_PTR, bsize*sizeof(float), out_cl_final_buf, kn_em);
//				m_dslt_kernel->setKernelArgConst(2, sizeof(int), (void*)(&ypitch), kn_em);
//				m_dslt_kernel->setKernelArgConst(3, sizeof(int), (void*)(&zpitch), kn_em);
//				m_dslt_kernel->execute(3, global_size, local_size, kn_em);
//							
//				delete[] cl_filter;
//				r /= 2;
//			} while (r >= 3);
//
//			m_dslt_kernel->readBuffer(out_cl_final_buf);
//
//			m_dslt_kernel->setKernelArgTex3D(0, CL_MEM_READ_ONLY, data_id, kn_b);
//			m_dslt_kernel->setKernelArgBuf(1, CL_MEM_READ_WRITE | CL_MEM_COPY_HOST_PTR, bsize*sizeof(float), out_cl_final_buf, kn_b);
//			m_dslt_kernel->setKernelArgBuf(2, CL_MEM_READ_WRITE | CL_MEM_COPY_HOST_PTR, bsize*sizeof(uint8), mask_temp_buf, kn_b);
//			m_dslt_kernel->setKernelArgConst(3, sizeof(float), (void*)(&cf), kn_b);
//			m_dslt_kernel->setKernelArgConst(4, sizeof(int), (void*)(&ypitch), kn_b);
//			m_dslt_kernel->setKernelArgConst(5, sizeof(int), (void*)(&zpitch), kn_b);
//			m_dslt_kernel->execute(3, global_size, local_size, kn_b);
//			m_dslt_kernel->readBuffer(mask_temp_buf);
///*
//			uint8* dst_p = mask_offset_pointer; 
//			float* src_p = out_cl_final_buf;
//			double vmax = 0.0;
//			for (int z = 0; z < brick_z; z++)
//			{
//					uint8* z_st_dst_p = dst_p;
//					for (int y = 0; y < brick_y; y++)
//					{
//						uint8* y_st_dst_p = dst_p;
//						for (int x = 0; x < brick_x; x++)
//						{
//							*dst_p = (uint8)(*src_p*255);
//							if (vmax < *src_p)
//								vmax = *src_p;
//							dst_p++;
//							src_p++;
//						}
//						dst_p = y_st_dst_p + mask_ypitch;
//					}
//					dst_p = z_st_dst_p + mask_zpitch;
//			}
//			vmax *= 65535.0;
//*/
//			if (bricks->size() == 1)
//				memcpy(mask->data, mask_temp_buf, bsize*sizeof(uint8));
//			else
//			{
//				uint8* dst_p = mask_offset_pointer; 
//				uint8* src_p = mask_temp_buf;
//				for (int z = 0; z < brick_z; z++)
//				{
//					uint8* z_st_dst_p = dst_p;
//					for (int y = 0; y < brick_y; y++)
//					{
//						memcpy(dst_p, src_p, brick_x*sizeof(uint8));
//						dst_p += mask_ypitch;
//						src_p += brick_x;
//					}
//					dst_p = z_st_dst_p + mask_zpitch;
//				}
//			}
//
//			delete[] out_cl_buf;
//			delete[] out_cl_final_buf;
//			delete[] n_cl_buf;
//			delete[] mask_temp_buf;
//		}
//
//		delete[] slatitable;
//		delete[] clatitable;
//		delete[] slongitable;
//		delete[] clongitable;
//		delete[] sintable;
//		delete[] costable;
//		delete[] sctptr;
//
//		clear_tex_pool();
//	}
//
//	//generate the labeling assuming the mask is already generated
//	//type: 0-initialization; 1-maximum intensity filtering
//	//mode: 0-normal; 1-posterized; 2-copy values; 3-poster, copy
//	void VolumeRenderer::draw_label(int type, int mode, double thresh, double gm_falloff)
//	{
//		vector<TextureBrick*> *bricks = tex_->get_bricks();
//		if (!bricks || bricks->size() == 0)
//			return;
//
//		glActiveTexture(GL_TEXTURE0);
//		//label frame buffer object
//		if (!glIsFramebuffer(fbo_label_))
//			glGenFramebuffers(1, &fbo_label_);
//		GLint cur_framebuffer_id;
//		glGetIntegerv(GL_FRAMEBUFFER_BINDING, &cur_framebuffer_id);
//		glBindFramebuffer(GL_FRAMEBUFFER, fbo_label_);
//
//		bool has_mask = tex_->nmask()!=-1;
//
//		//--------------------------------------------------------------------------
//		// Set up shaders
//		//seg shader
//		ShaderProgram* seg_shader = 0;
//
//		switch (type)
//		{
//		case 0://initialize
//			seg_shader = seg_shader_factory_.shader(
//				LBL_SHDR_INITIALIZE, mode, 0, has_mask,
//				false, false, false, hiqual_, false);
//			break;
//		case 1://maximum filter
//			seg_shader = seg_shader_factory_.shader(
//				LBL_SHDR_MIF, mode, 0, has_mask,
//				false, false, false, hiqual_, false);
//			break;
//		}
//
//		if (seg_shader)
//		{
//			if (!seg_shader->valid())
//				seg_shader->create();
//			seg_shader->bind();
//		}
//
//		//set uniforms
//		//set up shading
//		Vector light = compute_view().direction();
//		light.safe_normalize();
//		seg_shader->setLocalParam(0, light.x(), light.y(), light.z(), alpha_);
//		if (shading_)
//			seg_shader->setLocalParam(1, 2.0 - ambient_, diffuse_, specular_, shine_);
//		else
//			seg_shader->setLocalParam(1, 2.0 - ambient_, 0.0, specular_, shine_);
//
//		//spacings
//		double spcx, spcy, spcz;
//		tex_->get_spacings(spcx, spcy, spcz);
//		seg_shader->setLocalParam(5, spcx, spcy, spcz, 1.0);
//
//		//transfer function
//		seg_shader->setLocalParam(2, inv_?-scalar_scale_:scalar_scale_, gm_scale_, lo_thresh_, hi_thresh_);
//		seg_shader->setLocalParam(3, 1.0, gm_thresh_, offset_, sw_);
//		seg_shader->setLocalParam(6, color_.r(), color_.g(), color_.b(), 0.0);
//
//		if (type == 0)
//			seg_shader->setLocalParam(7, thresh, 0.0, 0.0, 0.0);
//		else if (type == 1)
//			//loc7: (initial thresh, gm_gaussian_falloff, scalar_gaussian_falloff, scalar_translate)
//			seg_shader->setLocalParam(7, 1.0, gm_falloff, 0.0, 0.0);
//
//		////////////////////////////////////////////////////////
//		// render bricks
//		glDisable(GL_DEPTH_TEST);
//
//		GLint vp[4];
//		glGetIntegerv(GL_VIEWPORT, vp);
//
//		for (unsigned int i=0; i < bricks->size(); i++)
//		{
//			TextureBrick* b = (*bricks)[i];
//
//			switch (type)
//			{
//			case 0://initialize
//				seg_shader->setLocalParam(9, 
//					b->nx(), 
//					b->ny(), 
//					b->nz(), 0.0f);//{nx, ny, nz, 0.0}
//				break;
//			case 1://maximum filter
//				seg_shader->setLocalParam(4, 
//					1.0/b->nx(),
//					1.0/b->ny(),
//					1.0/b->nz(), 0.0f);//{1/spcx, 1/spcy, 1/spcz, 0.0}
//				break;
//			}
//
//			//set viewport size the same as the texture
//			glViewport(0, 0, b->nx(), b->ny());
//
//			//load the texture
//			load_brick(0, 0, bricks, i, GL_NEAREST, compression_);
//			if (has_mask) load_brick_mask(bricks, i);
//			GLuint label_id = load_brick_label(bricks, i);
//
//			//draw each slice
//			int z;
//			for (z=0; z<b->nz(); z++)
//			{
//				glFramebufferTexture3D(GL_FRAMEBUFFER, 
//					GL_COLOR_ATTACHMENT0,
//					GL_TEXTURE_3D,
//					label_id,
//					0,
//					z);
//
//				draw_view_quad(double(z+0.5) / double(b->nz()));
//			}
//
//			b->set_dirty(b->nlabel(), true);
//		}
//
//		glViewport(vp[0], vp[1], vp[2], vp[3]);
//		////////////////////////////////////////////////////////
//
//		//release 3d mask
//		release_texture((*bricks)[0]->nmask(), GL_TEXTURE_3D);
//		//release 3d label
//		release_texture((*bricks)[0]->nlabel(), GL_TEXTURE_3D);
//
//		//release seg shader
//		if (seg_shader && seg_shader->valid())
//			seg_shader->release();
//
//		//unbind framebuffer
//		glBindFramebuffer(GL_FRAMEBUFFER, cur_framebuffer_id);
//
//		// Release texture
//		release_texture(0, GL_TEXTURE_3D);
//
//		// Reset the blend functions after MIP
//		glBlendEquation(GL_FUNC_ADD);
//		glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
//		glDisable(GL_BLEND);
//
//		//enable depth test
//		glEnable(GL_DEPTH_TEST);
//	}
//
//	double VolumeRenderer::calc_hist_3d(GLuint data_id, GLuint mask_id,
//		size_t brick_x, size_t brick_y, size_t brick_z)
//	{
//		double result = 0.0;
///*		KernelProgram* kernel = vol_kernel_factory_.kernel(KERNEL_HIST_3D);
//		if (kernel)
//		{
//			if (!kernel->valid())
//			{
//				string name = "hist_3d";
//				kernel->create(name);
//			}
//			kernel->setKernelArgTex3D(0, CL_MEM_READ_ONLY, data_id);
//			kernel->setKernelArgTex3D(1, CL_MEM_READ_ONLY, mask_id);
//			unsigned int hist_size = 64;
//			if (tex_ && tex_->get_nrrd(0))
//			{
//				if (tex_->get_nrrd(0)->type == nrrdTypeUChar)
//					hist_size = 64;
//				else if (tex_->get_nrrd(0)->type == nrrdTypeUShort)
//					hist_size = 1024;
//			}
//			float* hist = new float[hist_size];
//			memset(hist, 0, hist_size*sizeof(float));
//			kernel->setKernelArgBuf(2, CL_MEM_READ_WRITE|CL_MEM_COPY_HOST_PTR, hist_size*sizeof(float), hist);
//			kernel->setKernelArgConst(3, sizeof(unsigned int), (void*)(&hist_size));
//			size_t global_size[3] = {brick_x, brick_y, brick_z};
//			size_t local_size[3] = {1, 1, 1};
//			kernel->execute(3, global_size, local_size);
//			kernel->readBuffer(2, hist);
//			//analyze hist
//			int i;
//			float sum = 0;
//			for (i=0; i<hist_size; ++i)
//				sum += hist[i];
//			for (i=hist_size-1; i>0; --i)
//			{
//				if (hist[i] > sum/hist_size && hist[i] > hist[i-1])
//				{
//					result = double(i)/double(hist_size);
//					break;
//				}
//			}
//			////save hist
//			//ofstream outfile;
//			//outfile.open("E:\\hist.txt");
//			//for (int i=0; i<hist_size; ++i)
//			//{
//			//	float value = hist[i];
//			//	outfile << value << "\n";
//			//}
//			//outfile.close();
//
//			delete []hist;
//			VolumeRenderer::vol_kernel_factory_.clean();
//		}
//*/		return result;
//	}
//
//	//calculation
//	void VolumeRenderer::calculate(int type, FLIVR::VolumeRenderer *vr_a, FLIVR::VolumeRenderer *vr_b)
//	{
//		//sync sorting
//		Ray view_ray(Point(0.802,0.267,0.534), Vector(0.802,0.267,0.534));
//		tex_->set_sort_bricks();
//		vector<TextureBrick*> *bricks = tex_->get_sorted_bricks(view_ray);
//		if (!bricks || bricks->size() == 0)
//			return;
//		vector<TextureBrick*> *bricks_a = 0;
//		vector<TextureBrick*> *bricks_b = 0;
//
//		bricks_a = vr_a->tex_->get_bricks();
//		if (vr_a)
//		{
//			vr_a->tex_->set_sort_bricks();
//			bricks_a = vr_a->tex_->get_sorted_bricks(view_ray);
//		}
//		if (vr_b)
//		{
//			vr_b->tex_->set_sort_bricks();
//			bricks_b = vr_b->tex_->get_sorted_bricks(view_ray);
//		}
//
//		glActiveTexture(GL_TEXTURE0);
//		glEnable(GL_TEXTURE_2D);
//		glEnable(GL_TEXTURE_3D);
//		//mask frame buffer object
//		if (!glIsFramebuffer(fbo_label_))
//			glGenFramebuffers(1, &fbo_label_);
//		GLint cur_framebuffer_id;
//		glGetIntegerv(GL_FRAMEBUFFER_BINDING, &cur_framebuffer_id);
//		glBindFramebuffer(GL_FRAMEBUFFER, fbo_label_);
//
//		//--------------------------------------------------------------------------
//		// Set up shaders
//		//calculate shader
//		ShaderProgram* cal_shader = cal_shader_factory_.shader(type);
//
//		if (cal_shader)
//		{
//			if (!cal_shader->valid())
//				cal_shader->create();
//			cal_shader->bind();
//		}
//
//		//set uniforms
//		if (type==1 ||
//			type==2 ||
//			type==3 ||
//			type==4 ||
//			type==8)
//			cal_shader->setLocalParam(0, vr_a?vr_a->get_scalar_scale():1.0,
//				vr_b?vr_b->get_scalar_scale():1.0,
//				(vr_a&&vr_a->tex_&&vr_a->tex_->nmask()!=-1)?1.0:0.0,
//				(vr_b&&vr_b->tex_&&vr_b->tex_->nmask()!=-1)?1.0:0.0);
//		else
//			cal_shader->setLocalParam(0, vr_a?vr_a->get_scalar_scale():1.0,
//				vr_b?vr_b->get_scalar_scale():1.0,
//				(vr_a&&vr_a->get_inversion())?-1.0:0.0,
//				(vr_b&&vr_b->get_inversion())?-1.0:0.0);
//		if (vr_a && (type==6 || type==7))
//		{
//			cal_shader->setLocalParam(2, inv_?-scalar_scale_:scalar_scale_, gm_scale_, lo_thresh_, hi_thresh_);
//			cal_shader->setLocalParam(3, 1.0/gamma3d_, gm_thresh_, offset_, sw_);
//		}
//
//		for (unsigned int i=0; i < bricks->size(); i++)
//		{
//			TextureBrick* b = (*bricks)[i];
//
//			//set viewport size the same as the texture
//			glViewport(0, 0, b->nx(), b->ny());
//
//			//load the texture
//			GLuint tex_id = load_brick(0, 0, bricks, i, GL_NEAREST);
//			if (bricks_a) vr_a->load_brick(1, 0, bricks_a, i, GL_NEAREST);
//			if (bricks_b) vr_b->load_brick(2, 0, bricks_b, i, GL_NEAREST);
//			if ((type==5 || type==6 ||type==7) && bricks_a) vr_a->load_brick_mask(bricks_a, i, GL_NEAREST);
//			if (type==8)
//			{
//				if (bricks_a)
//					vr_a->load_brick_mask(bricks_a, i, GL_NEAREST, false, 3);
//				if (bricks_b)
//					vr_b->load_brick_mask(bricks_b, i, GL_NEAREST, false, 4);
//			}
//			//draw each slice
//			for (int z=0; z<b->nz(); z++)
//			{
//				glFramebufferTexture3D(GL_FRAMEBUFFER,
//					GL_COLOR_ATTACHMENT0,
//					GL_TEXTURE_3D,
//					tex_id,
//					0,
//					z);
//				draw_view_quad(double(z+0.5) / double(b->nz()));
//			}
//
//			b->set_dirty(0, true);
//		}
//
//		//release 3d mask
//		release_texture((*bricks)[0]->nmask(), GL_TEXTURE_3D);
//		release_texture(4, GL_TEXTURE_3D);
//		//release 3d label
//		release_texture((*bricks)[0]->nlabel(), GL_TEXTURE_3D);
//
//		//unbind framebuffer
//		glBindFramebuffer(GL_FRAMEBUFFER, cur_framebuffer_id);
//
//		//release seg shader
//		if (cal_shader && cal_shader->valid())
//			cal_shader->release();
//
//		glActiveTexture(GL_TEXTURE0);
//		glBindTexture(GL_TEXTURE_3D, 0);
//		//--------------------------------------------------------------------------
//
//		// Reset the blend functions after MIP
//		glBlendEquation(GL_FUNC_ADD);
//		glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
//
//		glDisable(GL_BLEND);
//	}
//
//	//return the data volume
//	void VolumeRenderer::return_volume()
//	{
//		if (!tex_)
//			return;
//		vector<TextureBrick*> *bricks = tex_->get_bricks();
//		if (!bricks || bricks->size() == 0)
//			return;
//
//		int c = 0;
//		for (unsigned int i=0; i<bricks->size(); i++)
//		{
//			load_brick(0, c, bricks, i, GL_NEAREST);
//			int nb = (*bricks)[i]->nb(c);
//			GLenum format;
//			if (nb < 3)
//				format = GL_RED;
//			else
//				format = GL_RGBA;
//
//			// download texture data
//			int sx = (*bricks)[i]->sx();
//			int sy = (*bricks)[i]->sy();
//			glPixelStorei(GL_PACK_ROW_LENGTH, sx);
//			glPixelStorei(GL_PACK_IMAGE_HEIGHT, sy);
//			glPixelStorei(GL_PACK_ALIGNMENT, 1);
//
//			GLenum type = (*bricks)[i]->tex_type(c);
//			void* data = (*bricks)[i]->tex_data(c);
//			glGetTexImage(GL_TEXTURE_3D, 0, format,
//				type, data);
//
//			glPixelStorei(GL_PACK_ROW_LENGTH, 0);
//			glPixelStorei(GL_PACK_IMAGE_HEIGHT, 0);
//			glPixelStorei(GL_PACK_ALIGNMENT, 4);
//
//			(*bricks)[i]->set_dirty(0, false);
//		}
//
//		//release 3d texture
//		glActiveTexture(GL_TEXTURE0);
//		glBindTexture(GL_TEXTURE_3D, 0);
//	}
//
//	//return the mask volume
//	void VolumeRenderer::return_mask()
//	{
//		if (!tex_)
//			return;
//		vector<TextureBrick*> *bricks = tex_->get_bricks();
//		if (!bricks || bricks->size() == 0)
//			return;
//
//		int c = tex_->nmask();
//		if (c<0 || c>=TEXTURE_MAX_COMPONENTS)
//			return;
//
//		for (unsigned int i=0; i<bricks->size(); i++)
//		{
//			if ((*bricks)[i]->dirty(c))
//			{
//				load_brick_mask(bricks, i, GL_NEAREST, false, 0, true, false);
//				glActiveTexture(GL_TEXTURE0+c);
//
//				// download texture data
//				int sx = (*bricks)[i]->sx();
//				int sy = (*bricks)[i]->sy();
//				glPixelStorei(GL_PACK_ROW_LENGTH, sx);
//				glPixelStorei(GL_PACK_IMAGE_HEIGHT, sy);
//				glPixelStorei(GL_PACK_ALIGNMENT, 1);
//
//				GLenum type = (*bricks)[i]->tex_type(c);
//				void* data = (*bricks)[i]->tex_data(c);
//				glGetTexImage(GL_TEXTURE_3D, 0, GL_RED, type, data);
//
//				glPixelStorei(GL_PACK_ROW_LENGTH, 0);
//				glPixelStorei(GL_PACK_IMAGE_HEIGHT, 0);
//				glPixelStorei(GL_PACK_ALIGNMENT, 4);
//
//				(*bricks)[i]->set_dirty((*bricks)[i]->nmask(), false);
//			}
//		}
//
//		/*
//		size_t nthreads = std::thread::hardware_concurrency();
//		if (nthreads > bricks->size()) nthreads = bricks->size();
//		std::vector<std::thread> threads(nthreads);
//		for(size_t t = 0; t < nthreads; t++)
//		{
//			threads[t] = std::thread(std::bind(
//				[&](const size_t bi, const size_t ei, const size_t t)
//			{
//				for(size_t i = bi; i<ei; i++)
//				{
//					load_brick_mask(bricks, i);
//					glActiveTexture(GL_TEXTURE0+c);
//
//					// download texture data
//					int sx = (*bricks)[i]->sx();
//					int sy = (*bricks)[i]->sy();
//					glPixelStorei(GL_PACK_ROW_LENGTH, sx);
//					glPixelStorei(GL_PACK_IMAGE_HEIGHT, sy);
//					glPixelStorei(GL_PACK_ALIGNMENT, 1);
//
//					GLenum type = (*bricks)[i]->tex_type(c);
//					void* data = (*bricks)[i]->tex_data(c);
//					glGetTexImage(GL_TEXTURE_3D, 0, GL_RED,
//						type, data);
//
//					glPixelStorei(GL_PACK_ROW_LENGTH, 0);
//					glPixelStorei(GL_PACK_IMAGE_HEIGHT, 0);
//					glPixelStorei(GL_PACK_ALIGNMENT, 4);
//				}
//			}, t*bricks->size()/nthreads, (t+1)==nthreads ? bricks->size() : (t+1)*bricks->size()/nthreads, t));
//		}
//		std::for_each(threads.begin(),threads.end(),[](std::thread& x){x.join();});
//		*/
//
//		//release mask texture
//		release_texture(c, GL_TEXTURE_3D);
//	}
//
//	//return the label volume
//	void VolumeRenderer::return_label()
//	{
//		if (!tex_)
//			return;
//		vector<TextureBrick*> *bricks = tex_->get_bricks();
//		if (!bricks || bricks->size() == 0)
//			return;
//
//		int c = tex_->nlabel();
//		if (c<0 || c>=TEXTURE_MAX_COMPONENTS)
//			return;
//
//		for (unsigned int i=0; i<bricks->size(); i++)
//		{
//			load_brick_label(bricks, i);
//			glActiveTexture(GL_TEXTURE0+c);
//
//			//download texture data
//			glPixelStorei(GL_PACK_ROW_LENGTH, (*bricks)[i]->sx());
//			glPixelStorei(GL_PACK_IMAGE_HEIGHT, (*bricks)[i]->sy());
//			glPixelStorei(GL_PACK_ALIGNMENT, 4);
//
//			glGetTexImage(GL_TEXTURE_3D, 0, GL_RED_INTEGER,
//				(*bricks)[i]->tex_type(c), (*bricks)[i]->tex_data(c));
//
//			glPixelStorei(GL_PACK_ROW_LENGTH, 0);
//			glPixelStorei(GL_PACK_IMAGE_HEIGHT, 0);
//			//glPixelStorei(GL_PACK_ALIGNMENT, 4);
//
//			(*bricks)[i]->set_dirty((*bricks)[i]->nlabel(), false);
//		}
//
//		//release label texture
//		release_texture(c, GL_TEXTURE_3D);
//	}
//
//	void VolumeRenderer::return_stroke()
//	{
//		if (!tex_)
//			return;
//		vector<TextureBrick*> *bricks = tex_->get_bricks();
//		if (!bricks || bricks->size() == 0)
//			return;
//
//		int c = tex_->nstroke();
//		if (c<0 || c>=TEXTURE_MAX_COMPONENTS)
//			return;
//
//		for (unsigned int i=0; i<bricks->size(); i++)
//		{
//			load_brick_stroke(bricks, i);
//			glActiveTexture(GL_TEXTURE0+c);
//
//			// download texture data
//			int sx = (*bricks)[i]->sx();
//			int sy = (*bricks)[i]->sy();
//			glPixelStorei(GL_PACK_ROW_LENGTH, sx);
//			glPixelStorei(GL_PACK_IMAGE_HEIGHT, sy);
//			glPixelStorei(GL_PACK_ALIGNMENT, 1);
//
//			GLenum type = (*bricks)[i]->tex_type(c);
//			void* data = (*bricks)[i]->tex_data(c);
//			glGetTexImage(GL_TEXTURE_3D, 0, GL_RED,
//				type, data);
//
//			glPixelStorei(GL_PACK_ROW_LENGTH, 0);
//			glPixelStorei(GL_PACK_IMAGE_HEIGHT, 0);
//			glPixelStorei(GL_PACK_ALIGNMENT, 4);
//			(*bricks)[i]->set_dirty((*bricks)[i]->nstroke(), false);
//		}
//
//		//release mask texture
//		release_texture(c, GL_TEXTURE_3D);
//	}
//
} // namespace FLIVR
