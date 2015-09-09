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
#include "utility.h"
#include "../compatibility.h"

#include <windows.h>//added by takashi

namespace FLIVR
{
#ifdef _WIN32
#undef min
#undef max
#endif

	VolShaderFactory TextureRenderer::vol_shader_factory_;
	SegShaderFactory TextureRenderer::seg_shader_factory_;
	VolCalShaderFactory TextureRenderer::cal_shader_factory_;
	ImgShaderFactory VolumeRenderer::m_img_shader_factory;
	double VolumeRenderer::sw_ = 0.0;

	VolumeRenderer::VolumeRenderer(Texture* tex,
		const vector<Plane*> &planes,
		bool hiqual)
		:TextureRenderer(tex),
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
		//solid
		solid_(false),
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
		compression_(false)
	{
		//mode
		mode_ = MODE_OVER;
		//done loop
		for (int i=0; i<TEXTURE_RENDER_MODES; i++)
			done_loop_[i] = false;
	}

	VolumeRenderer::VolumeRenderer(const VolumeRenderer& copy)
		:TextureRenderer(copy),
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
		//solid
		solid_(copy.solid_),
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
		compression_(copy.compression_)
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

	void VolumeRenderer::set_color(Color color)
	{
		color_ = color;

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

	Color VolumeRenderer::get_color()
	{
		return color_;
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
		irate_ = max(rate / 2.0, 0.1);
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
				double p = 0.012221;
				size = p*zoom;
				size = Clamp(size, 0.0, 0.25);
			}
			break;
		}

		//adjusting for screen size
		double af = vs/800.0;

		return size*af;
	}

	//darw
	void VolumeRenderer::draw(bool draw_wireframe_p, bool interactive_mode_p,
		bool orthographic_p, double zoom, bool intp, int mode, double sampling_frq_fac)
	{
		draw_volume(interactive_mode_p, orthographic_p, zoom, intp, mode, sampling_frq_fac);
		if(draw_wireframe_p)
			draw_wireframe(orthographic_p);
	}

	void VolumeRenderer::draw_volume(bool interactive_mode_p,
		bool orthographic_p,
		double zoom, bool intp,
		int mode, double sampling_frq_fac)
	{
		if (!tex_)
			return;

		Ray view_ray = compute_view();

		vector<TextureBrick*> *bricks = 0;
		if (mem_swap_ && interactive_)
			bricks = tex_->get_closest_bricks(
			quota_center_, quota_bricks_chan_, true,
			view_ray, orthographic_p);
		else
			bricks = tex_->get_sorted_bricks(view_ray, orthographic_p);
		if (!bricks || bricks->size() == 0)
			return;

		// Set sampling rate based on interaction
		double rate = imode_ ? irate_ : sampling_rate_;
		Vector diag = tex_->bbox()->diagonal();
/*		Vector cell_diag(
			diag.x() / tex_->nx(),
			diag.y() / tex_->ny(),
			diag.z() / tex_->nz());
		double dt = cell_diag.length()/compute_rate_scale()/rate;
*/		double dt = 0.0025/rate;
		num_slices_ = (int)(diag.length()/dt);

		double rate_fac = 1.0;

		if(1)
		{
			double maxlen;
			double vdmaxlen;
			Transform *field_trans = tex_->transform();

			double mvmat[16];
			glGetDoublev(GL_MODELVIEW_MATRIX, mvmat);
			Transform mv;
			mv.set_trans(mvmat);

			Vector spcv[3] = {Vector(1.0, 0.0, 0.0), Vector(0.0, 1.0, 0.0), Vector(0.0, 0.0, 1.0)};
			vdmaxlen = -1.0;

			for(int i = 0; i < 3 ; i++)
			{
				// index space view direction
				Vector v;
				v = field_trans->project(spcv[i]);
				v.safe_normalize();
				v = field_trans->project(spcv[i]);

				double len = Dot(spcv[i], v);;
				if(len > vdmaxlen) vdmaxlen = len;

			}
			
			if(sampling_frq_fac > 0.0)maxlen = sampling_frq_fac;
			else maxlen = vdmaxlen;

			// index space view direction
			Vector mv_ray = Vector(-mvmat[2], -mvmat[6], -mvmat[10]);//normalized
			Vector v = field_trans->project(Vector(-mvmat[2], -mvmat[6], -mvmat[10]));
			v.safe_normalize();
			v = field_trans->project(v);
			
			double l = Dot(mv_ray, v);
			dt = dt * maxlen / l;

			if(sampling_frq_fac > 0.0) rate_fac = sampling_frq_fac / vdmaxlen;
		}

		vector<double> vertex;
		vector<double> texcoord;
		vector<int> size;
		vertex.reserve(num_slices_*6);
		texcoord.reserve(num_slices_*6);
		size.reserve(num_slices_*6);

		//--------------------------------------------------------------------------
		//int nb0 = (*bricks)[0]->nc();

		bool use_fog = glIsEnabled(GL_FOG)!=0 && colormap_mode_!=2;
		GLfloat clear_color[4];
		glGetFloatv(GL_COLOR_CLEAR_VALUE, clear_color);
		GLint vp[4];
		glGetIntegerv(GL_VIEWPORT, vp);

		// set up blending
/*		glEnable(GL_BLEND);
		switch(mode_)
		{
		case MODE_OVER:
			glBlendEquation(GL_FUNC_ADD);
			if (update_order_ == 0)
				glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
			else if (update_order_ == 1)
				glBlendFunc(GL_ONE_MINUS_DST_ALPHA, GL_ONE);
			break;
		case MODE_MIP:
			glBlendEquation(GL_MAX);
			glBlendFunc(GL_ONE, GL_ONE);
			break;
		default:
			break;
		}
*/

		// Cache this value to reset, in case another framebuffer is active,
		// as it is in the case of saving an image from the viewer.
		GLint cur_framebuffer_id;
		glGetIntegerv(GL_FRAMEBUFFER_BINDING, &cur_framebuffer_id);
		GLint cur_draw_buffer;
		glGetIntegerv(GL_DRAW_BUFFER, &cur_draw_buffer);
		GLint cur_read_buffer;
		glGetIntegerv(GL_READ_BUFFER, &cur_read_buffer);

		int w = vp[2];
		int h = vp[3];
		int w2 = w;
		int h2 = h;

		double sf = CalcScaleFactor(w, h, tex_->nx(), tex_->ny(), zoom);
		if (fabs(sf-sfactor_)>0.05)
		{
			sfactor_ = sf;
			blend_framebuffer_resize_ = true;
			filter_buffer_resize_ = true;
		}
		else if (sf==1.0 && sfactor_!=1.0)
		{
			sfactor_ = sf;
			blend_framebuffer_resize_ = true;
			filter_buffer_resize_ = true;
		}

		w2 = int(w*sfactor_+0.5);
		h2 = int(h*sfactor_+0.5);

		if(blend_num_bits_ > 8)
		{
			if (!glIsFramebuffer(blend_framebuffer_))
			{
				glGenFramebuffers(1, &blend_framebuffer_);
				glGenTextures(1, &blend_tex_id_);
				glGenTextures(1, &label_tex_id_);

				glBindFramebuffer(GL_FRAMEBUFFER, blend_framebuffer_);

				// Initialize texture color renderbuffer
				glBindTexture(GL_TEXTURE_2D, blend_tex_id_);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
				glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, w2, h2, 0,
					GL_RGBA, GL_FLOAT, NULL);//GL_RGBA16F
				glFramebufferTexture2D(GL_FRAMEBUFFER,
					GL_COLOR_ATTACHMENT0,
					GL_TEXTURE_2D, blend_tex_id_, 0);
				glBindTexture(GL_TEXTURE_2D, 0);

				glBindTexture(GL_TEXTURE_2D, label_tex_id_);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
				glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, w2, h2, 0,
					GL_RGBA, GL_FLOAT, NULL);//GL_RGBA16F
				glFramebufferTexture2D(GL_FRAMEBUFFER,
					GL_COLOR_ATTACHMENT1,
					GL_TEXTURE_2D, label_tex_id_, 0);
				glBindTexture(GL_TEXTURE_2D, 0);
			}
			else
			{
				glBindFramebuffer(GL_FRAMEBUFFER, blend_framebuffer_);

				glFramebufferTexture2D(GL_FRAMEBUFFER,
					GL_COLOR_ATTACHMENT0,
					GL_TEXTURE_2D, blend_tex_id_, 0);
				
				glFramebufferTexture2D(GL_FRAMEBUFFER,
					GL_COLOR_ATTACHMENT1,
					GL_TEXTURE_2D, label_tex_id_, 0);
			}

			if (blend_framebuffer_resize_)
			{
				// resize texture color renderbuffer
				glBindTexture(GL_TEXTURE_2D, blend_tex_id_);
				glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, w2, h2, 0,
					GL_RGBA, GL_FLOAT, NULL);//GL_RGBA16F
				glBindTexture(GL_TEXTURE_2D, 0);

				glBindTexture(GL_TEXTURE_2D, label_tex_id_);
				glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, w2, h2, 0,
					GL_RGBA, GL_FLOAT, NULL);//GL_RGBA16F
				glBindTexture(GL_TEXTURE_2D, 0);

				blend_framebuffer_resize_ = false;
			}

			glBindTexture(GL_TEXTURE_2D, 0);
			glDisable(GL_TEXTURE_2D);

			glBindFramebuffer(GL_FRAMEBUFFER, blend_framebuffer_);

			static const GLenum draw_buffers[] =
			{
				GL_COLOR_ATTACHMENT0,
				GL_COLOR_ATTACHMENT1
			};
			glDrawBuffers(cur_chan_brick_num_ == 0 ? 2 : 1, draw_buffers);

			glClearColor(clear_color[0], clear_color[1], clear_color[2], clear_color[3]);
			glClear(GL_COLOR_BUFFER_BIT);

			glDrawBuffers(2, draw_buffers);

			glViewport(vp[0], vp[1], w2, h2);
		}
		
		glEnablei(GL_BLEND, 0);
		switch(mode_)
		{
		case MODE_OVER:
			glBlendEquationi(0, GL_FUNC_ADD);
			if (update_order_ == 0)
				glBlendFunci(0, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
			else if (update_order_ == 1)
				glBlendFunci(0, GL_ONE_MINUS_DST_ALPHA, GL_ONE);
			break;
		case MODE_MIP:
			glBlendEquationi(0, GL_MAX);
			glBlendFunci(0, GL_ONE, GL_ONE);
			break;
		default:
			break;
		}
		glDisablei(GL_BLEND, 1);
		

		if(glIsTexture(label_tex_id_)){
			glActiveTexture(GL_TEXTURE5);
			glEnable(GL_TEXTURE_2D);
			glBindTexture(GL_TEXTURE_2D, label_tex_id_);
			glActiveTexture(GL_TEXTURE0);
		}

		//disable depth test
		glDisable(GL_DEPTH_TEST);

		//enable 3d texture
		glEnable(GL_TEXTURE_3D);

		//--------------------------------------------------------------------------
		// enable data texture unit 0
		glActiveTexture(GL_TEXTURE0);

		//reassess the mask/label mode
		//0-normal, 1-render with mask, 2-render with mask excluded
		//3-random color with label, 4-random color with label+mask

		//ml_mode_ = 4;

		switch (ml_mode_)
		{
		case 0:
			mask_ = false;
			label_ = false;
			break;
		case 1:
			if (tex_->nmask()==-1)
			{
				mask_ = false;
				ml_mode_ = 0;
			}
			else
				mask_ = true;
			label_ = false;
			break;
		case 2:
			if (tex_->nmask()==-1)
			{
				mask_ = false;
				ml_mode_ = 0;
			}
			else
				mask_ = true;
			label_ = false;
			break;
		case 3:
			if (tex_->nlabel()==-1)
			{
				label_ = false;
				ml_mode_ = 0;
			}
			else
				label_ = true;
			mask_ = false;
			break;
		case 4:
			if (tex_->nlabel()==-1)
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

		//--------------------------------------------------------------------------
		// Set up shaders
		FragmentProgram* shader = 0;
		//create/bind
		shader = vol_shader_factory_.shader(
			tex_->nc(),
			shading_, use_fog,
			depth_peel_, true,
			hiqual_, ml_mode_,
			colormap_mode_, solid_);
		if (shader)
		{
			if (!shader->valid())
				shader->create();
			shader->bind();
		}

		//set uniforms
		//set up shading
		double mat[16];
		glGetDoublev(GL_MODELVIEW_MATRIX, mat);
		Transform mv;
		mv.set_trans(mat);
		Vector light = view_ray.direction();
		light.safe_normalize();
		shader->setLocalParam(0, light.x(), light.y(), light.z(), alpha_);
		if (shading_)
			shader->setLocalParam(1, 2.0 - ambient_, diffuse_, specular_, shine_);
		else
			shader->setLocalParam(1, 2.0 - ambient_, 0.0, specular_, shine_);

		//spacings
		double spcx, spcy, spcz;
		tex_->get_spacings(spcx, spcy, spcz);
		shader->setLocalParam(5, spcx, spcy, spcz, 1.0);

		//transfer function
		shader->setLocalParam(2, inv_?-scalar_scale_:scalar_scale_, gm_scale_, lo_thresh_, hi_thresh_);
		shader->setLocalParam(3, 1.0/gamma3d_, gm_thresh_, offset_, sw_);
		switch (colormap_mode_)
		{
		case 0://normal
			if (mask_ && !label_)
				shader->setLocalParam(6, mask_color_.r(), mask_color_.g(), mask_color_.b(), mask_thresh_);
			else
				shader->setLocalParam(6, color_.r(), color_.g(), color_.b(), 0.0);
			break;
		case 1://colormap
			shader->setLocalParam(6, colormap_low_value_, colormap_hi_value_,
				colormap_hi_value_-colormap_low_value_, 0.0);
			break;
		case 3://indexed color
			shader->setLocalParam(6, double(w2), double(h2), 0.0, 0.0);
			break;
		}

		//setup depth peeling
		if (depth_peel_ || colormap_mode_ == 2)
			shader->setLocalParam(7, 1.0/double(w2), 1.0/double(h2), 0.0, 0.0);


		//set clipping planes
		double abcd[4];
		planes_[0]->get(abcd);
		shader->setLocalParam(10, abcd[0], abcd[1], abcd[2], abcd[3]);
		planes_[1]->get(abcd);
		shader->setLocalParam(11, abcd[0], abcd[1], abcd[2], abcd[3]);
		planes_[2]->get(abcd);
		shader->setLocalParam(12, abcd[0], abcd[1], abcd[2], abcd[3]);
		planes_[3]->get(abcd);
		shader->setLocalParam(13, abcd[0], abcd[1], abcd[2], abcd[3]);
		planes_[4]->get(abcd);
		shader->setLocalParam(14, abcd[0], abcd[1], abcd[2], abcd[3]);
		planes_[5]->get(abcd);
		shader->setLocalParam(15, abcd[0], abcd[1], abcd[2], abcd[3]);

		//takashi_debug
		//ofstream ofs1;
		//ofs1.open("matdraw_volume_pre.txt");
		//double mmat[16];
		//glGetDoublev(GL_MODELVIEW_MATRIX, mmat);
		//for(int n = 0; n < 16; n++)ofs1 << mmat[n] << endl;
		//ofs1.close();

		////////////////////////////////////////////////////////
		// render bricks
		// Set up transform
		Transform *tform = tex_->transform();
		double mvmat[16];
		tform->get_trans(mvmat);
		glMatrixMode(GL_MODELVIEW);
		glPushMatrix();
		glMultMatrixd(mvmat);
		float matrix[16];
		
		//takashi_debug
		//ofstream ofs;
		//ofs.open("matdraw_volume.txt");
		//glGetDoublev(GL_MODELVIEW_MATRIX, mvmat);
		//for(int n = 0; n < 16; n++)ofs << mvmat[n] << endl;
		//ofs.close();

		if (cur_chan_brick_num_ == 0) rearrangeLoadedBrkVec();

		timeBeginPeriod(1);
		timeEndPeriod(1);

		int bmode = mode;
		if (mask_)  bmode = TEXTURE_RENDER_MODE_MASK;
		if (label_) bmode = TEXTURE_RENDER_MODE_LABEL;

		for (unsigned int i=0; i < bricks->size(); i++)
		{
			//comment off when debug_ds
			
			if (mem_swap_)
			{
				uint32_t rn_time = GET_TICK_COUNT();
				if (rn_time - st_time_ > get_up_time())
					break;
			}
			
			TextureBrick* b = (*bricks)[i];
			if (mem_swap_ && start_update_loop_ && !done_update_loop_)
			{
				if (b->drawn(bmode))
					continue;
			}

			if (!test_against_view(b->bbox()) || // Clip against view
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
			texcoord.clear();
			size.clear();
			b->compute_polygons(view_ray, dt, vertex, texcoord, size);

			if (vertex.size() == 0){
				cur_chan_brick_num_++;
				continue;
			}
			GLint filter;
			if (intp && colormap_mode_ != 3)
				filter = GL_LINEAR;
			else
				filter = GL_NEAREST;

			if (!load_brick(0, 0, bricks, i, filter, compression_, mode))
				continue;
			if (mask_)
				load_brick_mask(bricks, i, filter);
			if (label_)
				load_brick_label(bricks, i);
			/*
			double spc[3];
			tex_->get_spacings(spc[0], spc[1], spc[2]);
			
			double minspc = DBL_MAX;
			for(int j = 0; j < 3; j++)
			{
				if(spc[j] < minspc)minspc = spc[j];
			}
			shader->setLocalParam(4, 1.0/(b->nx()*spc[0]/minspc), 1.0/(b->ny()*spc[1]/minspc), 1.0/(b->nz()*spc[2]/minspc),
				mode_==MODE_OVER?1.0/rate*rate_fac:1.0);
			*/
			/*
			double maxspc = -1;
			for(int j = 0; j < 3; j++)
			{
				if(spc[j] > maxspc)maxspc = spc[j];
			}
			shader->setLocalParam(4, 1.0/(b->nx()*spc[0]/maxspc), 1.0/(b->ny()*spc[1]/maxspc), 1.0/(b->nz()*spc[2]/maxspc),
				mode_==MODE_OVER?1.0/rate*rate_fac:1.0);
			*/
			shader->setLocalParam(4, 1.0/b->nx(), 1.0/b->ny(), 1.0/b->nz(),
				mode_==MODE_OVER?1.0/rate*rate_fac:1.0);

			//for brick transformation
			BBox dbox = b->dbox();
			matrix[0] = float(dbox.max().x()-dbox.min().x());
			matrix[1] = 0.0f;
			matrix[2] = 0.0f;
			matrix[3] = 0.0f;
			matrix[4] = 0.0f;
			matrix[5] = float(dbox.max().y()-dbox.min().y());
			matrix[6] = 0.0f;
			matrix[7] = 0.0f;
			matrix[8] = 0.0f;
			matrix[9] = 0.0f;
			matrix[10] = float(dbox.max().z()-dbox.min().z());
			matrix[11] = 0.0f;
			matrix[12] = float(dbox.min().x());
			matrix[13] = float(dbox.min().y());
			matrix[14] = float(dbox.min().z());
			matrix[15] = 1.0f;
			shader->setLocalParamMatrix(2, matrix);

			//takashi_debug
//			LARGE_INTEGER liBegin,liEnd,liFreq;
//			QueryPerformanceCounter(&liBegin);

			draw_polygons(vertex, texcoord, size, use_fog, shader);
/*
			{
				//transformations
				glMatrixMode(GL_PROJECTION);
				glPushMatrix();
				glLoadIdentity();
				glMatrixMode(GL_MODELVIEW);
				glPushMatrix();
				glLoadIdentity();

				glBindFramebuffer(GL_FRAMEBUFFER, blend_framebuffer_); 
				glFramebufferTexture2D(GL_FRAMEBUFFER,
					GL_COLOR_ATTACHMENT0,
					GL_TEXTURE_2D, label_tex_id_copy_, 0);
				glBindTexture(GL_TEXTURE_2D, label_tex_id_);
				glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
				static const GLenum draw_buf = GL_COLOR_ATTACHMENT0;
				glDrawBuffers(1, &draw_buf);
				glBegin(GL_QUADS);
				{
					glTexCoord2f(0.0, 0.0);
					glVertex3f(-1, -1, 0.0);
					glTexCoord2f(1.0, 0.0);
					glVertex3f(1, -1, 0.0);
					glTexCoord2f(1.0, 1.0);
					glVertex3f(1, 1, 0.0);
					glTexCoord2f(0.0, 1.0);
					glVertex3f(-1, 1, 0.0);
				}
				glEnd();
				glBindTexture(GL_TEXTURE_2D, 0);

				glFramebufferTexture2D(GL_FRAMEBUFFER,
					GL_COLOR_ATTACHMENT0,
					GL_TEXTURE_2D, blend_tex_id_, 0);
				glDrawBuffers(2, draw_buffers);

				glMatrixMode(GL_PROJECTION);
				glPopMatrix();
				glMatrixMode(GL_MODELVIEW);
				glPopMatrix();
			}
*/
			
			//takashi_debug
/*			if(i == 0 && !mask_){
				ofstream ofs;
				ofs.open("draw_shader.txt");
				ofs << shader->getProgram() << endl;
				ofs.close();
			}

			if(i == 0 && mask_){
				ofstream ofs;
				ofs.open("draw_mask_shader.txt");
				ofs << shader->getProgram() << endl;
				ofs.close();
			}
*/			
			if (mem_swap_){
				finished_bricks_++;
				glFinish();//Added by takashi
			}

			//takashi_debug
/*			QueryPerformanceCounter(&liEnd);

			QueryPerformanceFrequency(&liFreq);
			
			ofstream ofs;
			ofs.open("draw_time.txt", ios::app);
			ofs << (double)(liEnd.QuadPart - liBegin.QuadPart) / (double)liFreq.QuadPart << endl;
			ofs.close();
*/
			////this is for debug_ds, comment when done
			//if (mem_swap_)
			//{
			//	uint32_t rn_time = GET_TICK_COUNT();
			//	if (rn_time - st_time_ > get_up_time())
			//		break;
			//}
		}
		
		//takashi_debug
		//if (mem_swap_ &&
		//	cur_brick_num_ > total_brick_num_)
		//{
		//	done_update_loop_ = true;
		//}

		if (mem_swap_ &&
			cur_brick_num_ == total_brick_num_)
		{
			done_update_loop_ = true;
		}
		if (mem_swap_ &&
			cur_chan_brick_num_ == (*bricks).size())
		{
			done_current_chan_ = true;
			clear_chan_buffer_ = true;
			save_final_buffer_ = true;
			cur_chan_brick_num_ = 0;
			done_loop_[bmode] = true;
		}

		glFinish();

		// Undo transform.
		glPopMatrix();
		////////////////////////////////////////////////////////
		{		
			if(glIsTexture(label_tex_id_)){
				glActiveTexture(GL_TEXTURE5);
				glBindTexture(GL_TEXTURE_2D, 0);
				glDisable(GL_TEXTURE_2D);
				glActiveTexture(GL_TEXTURE0);
			}
			glBindFramebuffer(GL_FRAMEBUFFER, blend_framebuffer_);
			glFramebufferTexture2D(GL_FRAMEBUFFER,
				GL_COLOR_ATTACHMENT1,
				GL_TEXTURE_2D, 0, 0);
			static const GLenum draw_buf = GL_COLOR_ATTACHMENT0;
			glDrawBuffers(1, &draw_buf);
			glBindFramebuffer(GL_FRAMEBUFFER, 0);
		}

		//release depth texture for rendering shadows
		if (colormap_mode_ == 2)
			release_texture(4, GL_TEXTURE_2D);

		// Release shader.
		if (shader && shader->valid())
			shader->release();

		//Release 3d texture
		release_texture(0, GL_TEXTURE_3D);

		//enable
		glEnable(GL_DEPTH_TEST);

		//output result
		if (blend_num_bits_ > 8)
		{
			//states
			GLboolean depth_test = glIsEnabled(GL_DEPTH_TEST);
			GLboolean lighting = glIsEnabled(GL_LIGHTING);
			GLboolean cull_face = glIsEnabled(GL_CULL_FACE);
			glDisable(GL_DEPTH_TEST);
			glDisable(GL_LIGHTING);
			glDisable(GL_CULL_FACE);
			glActiveTexture(GL_TEXTURE0);
			glEnable(GL_TEXTURE_2D);

			//transformations
			glMatrixMode(GL_PROJECTION);
			glPushMatrix();
			glLoadIdentity();
			glMatrixMode(GL_MODELVIEW);
			glPushMatrix();
			glLoadIdentity();

			FragmentProgram* img_shader = 0;

			if (noise_red_ && colormap_mode_!=2)
			{
				//FILTERING/////////////////////////////////////////////////////////////////
				if (!glIsTexture(filter_tex_id_))
				{
					glGenTextures(1, &filter_tex_id_);
					glBindTexture(GL_TEXTURE_2D, filter_tex_id_);
					glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
					glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
					glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
					glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
					glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, w2, h2, 0,
						GL_RGBA, GL_FLOAT, NULL);//GL_RGBA16F
				}
				if (!glIsFramebuffer(filter_buffer_))
				{
					glGenFramebuffers(1, &filter_buffer_);
					glBindFramebuffer(GL_FRAMEBUFFER, filter_buffer_);
					glFramebufferTexture2D(GL_FRAMEBUFFER,
						GL_COLOR_ATTACHMENT0,
						GL_TEXTURE_2D, filter_tex_id_, 0);
				}
				if (filter_buffer_resize_)
				{
					glBindTexture(GL_TEXTURE_2D, filter_tex_id_);
					glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, w2, h2, 0,
						GL_RGBA, GL_FLOAT, NULL);//GL_RGBA16F
					filter_buffer_resize_ = false;
				}

				glBindFramebuffer(GL_FRAMEBUFFER, filter_buffer_);
				glClear(GL_COLOR_BUFFER_BIT);

				glBindTexture(GL_TEXTURE_2D, blend_tex_id_);
				glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);

				img_shader = 
					m_img_shader_factory.shader(IMG_SHDR_FILTER_SMOOTH_MIN);
				if (img_shader)
				{
					if (!img_shader->valid())
					{
						img_shader->create();
					}
					img_shader->bind();
				}
				filter_size_min_ = CalcFilterSize(1, w, h, tex_->nx(), tex_->ny(), zoom, sfactor_);
				img_shader->setLocalParam(0, filter_size_min_/w2, filter_size_min_/h2, 0.5, 0.0);

				glBegin(GL_QUADS);
				{
					glTexCoord2f(0.0, 0.0);
					glVertex3f(-1, -1, 0.0);
					glTexCoord2f(1.0, 0.0);
					glVertex3f(1, -1, 0.0);
					glTexCoord2f(1.0, 1.0);
					glVertex3f(1, 1, 0.0);
					glTexCoord2f(0.0, 1.0);
					glVertex3f(-1, 1, 0.0);
				}
				glEnd();
				if (img_shader && img_shader->valid())
					img_shader->release();

				//
				glBindFramebuffer(GL_FRAMEBUFFER, blend_framebuffer_); 

				glBindTexture(GL_TEXTURE_2D, filter_tex_id_);
				glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
				img_shader = 
					m_img_shader_factory.shader(IMG_SHDR_FILTER_SMOOTH_MAX);
				if (img_shader)
				{
					if (!img_shader->valid())
					{
						img_shader->create();
					}
					img_shader->bind();
				}
				filter_size_max_ = CalcFilterSize(2, w, h, tex_->nx(), tex_->ny(), zoom, sfactor_);
				img_shader->setLocalParam(0, filter_size_max_/w, filter_size_max_/h, 1.0, 0.0);
				glBegin(GL_QUADS);
				{
					glTexCoord2f(0.0, 0.0);
					glVertex3f(-1, -1, 0.0);
					glTexCoord2f(1.0, 0.0);
					glVertex3f(1, -1, 0.0);
					glTexCoord2f(1.0, 1.0);
					glVertex3f(1, 1, 0.0);
					glTexCoord2f(0.0, 1.0);
					glVertex3f(-1, 1, 0.0);
				}
				glEnd();
				if (img_shader && img_shader->valid())
					img_shader->release();
				///////////////////////////////////////////////////////////////////////////
			}

			//go back to normal
			glBindFramebuffer(GL_FRAMEBUFFER, cur_framebuffer_id); 
			glDrawBuffer(cur_draw_buffer);
			glReadBuffer(cur_read_buffer);

			glViewport(vp[0], vp[1], vp[2], vp[3]);

			glBindTexture(GL_TEXTURE_2D, blend_tex_id_);
			glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);

			if (noise_red_ && colormap_mode_!=2)
			{
				img_shader = 
					m_img_shader_factory.shader(IMG_SHDR_FILTER_SHARPEN);
				if (img_shader)
				{
					if (!img_shader->valid())
					{
						img_shader->create();
					}
					img_shader->bind();
				}
				filter_size_shp_ = CalcFilterSize(3, w, h, tex_->nx(), tex_->ny(), zoom, sfactor_);
				img_shader->setLocalParam(0, filter_size_shp_/w, filter_size_shp_/h, 0.0, 0.0);
			}

			glBegin(GL_QUADS);
			{
				glTexCoord2f(0.0, 0.0);
				glVertex3f(-1, -1, 0.0);
				glTexCoord2f(1.0, 0.0);
				glVertex3f(1, -1, 0.0);
				glTexCoord2f(1.0, 1.0);
				glVertex3f(1, 1, 0.0);
				glTexCoord2f(0.0, 1.0);
				glVertex3f(-1, 1, 0.0);
			}
			glEnd();

			if (noise_red_ && colormap_mode_!=2)
			{
				if (img_shader && img_shader->valid())
					img_shader->release();
			}

			if (depth_test) glEnable(GL_DEPTH_TEST);
			if (lighting) glEnable(GL_LIGHTING);
			if (cull_face) glEnable(GL_CULL_FACE);

			glMatrixMode(GL_PROJECTION);
			glPopMatrix();
			glMatrixMode(GL_MODELVIEW);
			glPopMatrix();
			glBindTexture(GL_TEXTURE_2D, 0);
			glDisable(GL_TEXTURE_2D);
		}

		// Reset the blend functions after MIP
		glBlendEquation(GL_FUNC_ADD);
		glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
		glDisable(GL_BLEND);
	}

	void VolumeRenderer::draw_wireframe(bool orthographic_p)
	{
		Ray view_ray = compute_view();
		Transform *tform = tex_->transform();
		double mvmat[16];
		tform->get_trans(mvmat);
		glMatrixMode(GL_MODELVIEW);
		glPushMatrix();
		glMultMatrixd(mvmat);
		glEnable(GL_DEPTH_TEST);
		GLboolean lighting = glIsEnabled(GL_LIGHTING);
		glDisable(GL_LIGHTING);
		glDisable(GL_TEXTURE_3D);
		glDisable(GL_TEXTURE_2D);
		glDisable(GL_FOG);
		vector<TextureBrick*> *bricks = tex_->get_sorted_bricks(view_ray, orthographic_p);

		double rate = imode_ ? irate_ : sampling_rate_;
		Vector diag = tex_->bbox()->diagonal();
		Vector cell_diag(diag.x()/tex_->nx(),
			diag.y()/tex_->ny(),
			diag.z()/tex_->nz());
		double dt = cell_diag.length()/compute_rate_scale()/rate;
		num_slices_ = (int)(diag.length()/dt);

		vector<double> vertex;
		vector<double> texcoord;
		vector<int> size;
		vertex.reserve(num_slices_*6);
		texcoord.reserve(num_slices_*6);
		size.reserve(num_slices_*6);

		if (bricks)
		{
			for (unsigned int i=0; i<bricks->size(); i++)
			{
				glColor4d(float(0.8*(i+1.0)/bricks->size()), float(0.8*(i+1.0)/bricks->size()), 0.8f, 1.0f);

				TextureBrick* b = (*bricks)[i];
				if (!test_against_view(b->bbox())) continue;

				Point pmin(b->bbox().min());
				Point pmax(b->bbox().max());
				Point corner[8];
				corner[0] = pmin;
				corner[1] = Point(pmin.x(), pmin.y(), pmax.z());
				corner[2] = Point(pmin.x(), pmax.y(), pmin.z());
				corner[3] = Point(pmin.x(), pmax.y(), pmax.z());
				corner[4] = Point(pmax.x(), pmin.y(), pmin.z());
				corner[5] = Point(pmax.x(), pmin.y(), pmax.z());
				corner[6] = Point(pmax.x(), pmax.y(), pmin.z());
				corner[7] = pmax;

				glBegin(GL_LINES);
				{
					for(int i=0; i<4; i++) {
						glVertex3d(corner[i].x(), corner[i].y(), corner[i].z());
						glVertex3d(corner[i+4].x(), corner[i+4].y(), corner[i+4].z());
					}
				}
				glEnd();
				glBegin(GL_LINE_LOOP);
				{
					glVertex3d(corner[0].x(), corner[0].y(), corner[0].z());
					glVertex3d(corner[1].x(), corner[1].y(), corner[1].z());
					glVertex3d(corner[3].x(), corner[3].y(), corner[3].z());
					glVertex3d(corner[2].x(), corner[2].y(), corner[2].z());
				}
				glEnd();
				glBegin(GL_LINE_LOOP);
				{
					glVertex3d(corner[4].x(), corner[4].y(), corner[4].z());
					glVertex3d(corner[5].x(), corner[5].y(), corner[5].z());
					glVertex3d(corner[7].x(), corner[7].y(), corner[7].z());
					glVertex3d(corner[6].x(), corner[6].y(), corner[6].z());
				}
				glEnd();

				glColor4d(0.4, 0.4, 0.4, 1.0);

				vertex.clear();
				texcoord.clear();
				size.clear();

				// Scale out dt such that the slices are artificially further apart.
				b->compute_polygons(view_ray, dt * 10, vertex, texcoord, size);
				draw_polygons_wireframe(vertex, texcoord, size, false);
			}
		}

		if(lighting) glEnable(GL_LIGHTING);
		glMatrixMode(GL_MODELVIEW);
		glPopMatrix();
	}

	//type: 0-initial; 1-diffusion-based growing; 2-masked filtering
	//paint_mode: 1-select; 2-append; 3-erase; 4-diffuse; 5-flood; 6-clear; 7-all;
	//			  11-posterize
	//hr_mode (hidden removal): 0-none; 1-ortho; 2-persp
	void VolumeRenderer::draw_mask(int type, int paint_mode, int hr_mode,
		double ini_thresh, double gm_falloff, double scl_falloff,
		double scl_translate, double w2d, double bins, bool orthographic_p)
	{
		bool use_2d = glIsTexture(tex_2d_weight1_)&&
			glIsTexture(tex_2d_weight2_)?true:false;

		Ray view_ray = compute_view();

		vector<TextureBrick*> *bricks = tex_->get_sorted_bricks(view_ray, orthographic_p);
		if (!bricks || bricks->size() == 0)
			return;

		glActiveTexture(GL_TEXTURE0);
		glEnable(GL_TEXTURE_2D);
		glEnable(GL_TEXTURE_3D);
		//mask frame buffer object
		if (!glIsFramebuffer(fbo_mask_))
			glGenFramebuffers(1, &fbo_mask_);
		GLint cur_framebuffer_id;
		glGetIntegerv(GL_FRAMEBUFFER_BINDING, &cur_framebuffer_id);
		glBindFramebuffer(GL_FRAMEBUFFER, fbo_mask_);

		//--------------------------------------------------------------------------
		// Set up shaders
		//seg shader
		FragmentProgram* seg_shader = 0;

		switch (type)
		{
		case 0://initialize
			seg_shader = seg_shader_factory_.shader(
				SEG_SHDR_INITIALIZE, paint_mode, hr_mode,
				use_2d, true, depth_peel_, true, hiqual_);
			break;
		case 1://diffusion-based growing
			seg_shader = seg_shader_factory_.shader(
				SEG_SHDR_DB_GROW, paint_mode, hr_mode,
				use_2d, true, depth_peel_, true, hiqual_);
			break;
		case 2://noise removal
			seg_shader = seg_shader_factory_.shader(
				FLT_SHDR_NR, paint_mode, hr_mode,
				false, false, depth_peel_, false, hiqual_);
			break;
		}

		if (seg_shader)
		{
			if (!seg_shader->valid())
				seg_shader->create();
			seg_shader->bind();
		}
/*
		ofstream ofs;
		ofs.open("draw_mask_ini_shader.txt");
		ofs << seg_shader->getProgram() << endl;
		ofs.close();
*/
		//set uniforms
		//set up shading
		double mat[16];
		glGetDoublev(GL_MODELVIEW_MATRIX, mat);
		Transform mv;
		mv.set_trans(mat);
		Vector light = compute_view().direction();
		light.safe_normalize();
		seg_shader->setLocalParam(0, light.x(), light.y(), light.z(), alpha_);
		if (shading_)
			seg_shader->setLocalParam(1, 2.0 - ambient_, diffuse_, specular_, shine_);
		else
			seg_shader->setLocalParam(1, 2.0 - ambient_, 0.0, specular_, shine_);

		//spacings
		double spcx, spcy, spcz;
		tex_->get_spacings(spcx, spcy, spcz);
		seg_shader->setLocalParam(5, spcx, spcy, spcz, 1.0);

		//transfer function
		seg_shader->setLocalParam(2, inv_?-scalar_scale_:scalar_scale_, gm_scale_, lo_thresh_, hi_thresh_);
		seg_shader->setLocalParam(3, 1.0/gamma3d_, gm_thresh_, offset_, sw_);
		seg_shader->setLocalParam(6, color_.r(), color_.g(), color_.b(), 0.0);

		//setup depth peeling
		//if (depth_peel_)
		//	seg_shader->setLocalParam(7, 1.0/double(w2), 1.0/double(h2), 0.0, 0.0);

		//thresh1
		seg_shader->setLocalParam(7, ini_thresh, gm_falloff, scl_falloff, scl_translate);
		//w2d
		seg_shader->setLocalParam(8, w2d, bins, 0.0, 0.0);

		//set clipping planes
		double abcd[4];
		planes_[0]->get(abcd);
		seg_shader->setLocalParam(10, abcd[0], abcd[1], abcd[2], abcd[3]);
		planes_[1]->get(abcd);
		seg_shader->setLocalParam(11, abcd[0], abcd[1], abcd[2], abcd[3]);
		planes_[2]->get(abcd);
		seg_shader->setLocalParam(12, abcd[0], abcd[1], abcd[2], abcd[3]);
		planes_[3]->get(abcd);
		seg_shader->setLocalParam(13, abcd[0], abcd[1], abcd[2], abcd[3]);
		planes_[4]->get(abcd);
		seg_shader->setLocalParam(14, abcd[0], abcd[1], abcd[2], abcd[3]);
		planes_[5]->get(abcd);
		seg_shader->setLocalParam(15, abcd[0], abcd[1], abcd[2], abcd[3]);

		////////////////////////////////////////////////////////
		// render bricks
		// Set up transform
		Transform *tform = tex_->transform();
		double mvmat[16];
		tform->get_trans(mvmat);
		glMatrixMode(GL_MODELVIEW);
		glPushMatrix();
		glMultMatrixd(mvmat);

		//bind 2d mask texture
		bind_2d_mask();
		//bind 2d weight map
		if (use_2d) bind_2d_weight();

		//get transformation
		float matrix[16];
		glGetFloatv(GL_MODELVIEW_MATRIX, matrix);
		seg_shader->setLocalParamMatrix(0, matrix);
		glGetFloatv(GL_PROJECTION_MATRIX, matrix);
		seg_shader->setLocalParamMatrix(1, matrix);
		if (hr_mode > 0)
		{
			double dmatrix[16];
			Transform mv;
			glGetDoublev(GL_MODELVIEW_MATRIX, dmatrix);
			mv.set(dmatrix);
			mv.invert();
			mv.get(matrix);
			seg_shader->setLocalParamMatrix(3, matrix);
		}

		glMatrixMode(GL_MODELVIEW);
		glPushMatrix();
		glLoadIdentity();
		glMatrixMode(GL_PROJECTION);
		glPushMatrix();
		glLoadIdentity();
		glDisable(GL_DEPTH_TEST);

		GLint vp[4];
		glGetIntegerv(GL_VIEWPORT, vp);

		for (unsigned int i=0; i < bricks->size(); i++)
		{
			TextureBrick* b = (*bricks)[i];

			BBox bbox = b->bbox();
			matrix[0] = float(bbox.max().x()-bbox.min().x());
			matrix[1] = 0.0f;
			matrix[2] = 0.0f;
			matrix[3] = 0.0f;
			matrix[4] = 0.0f;
			matrix[5] = float(bbox.max().y()-bbox.min().y());
			matrix[6] = 0.0f;
			matrix[7] = 0.0f;
			matrix[8] = 0.0f;
			matrix[9] = 0.0f;
			matrix[10] = float(bbox.max().z()-bbox.min().z());
			matrix[11] = 0.0f;
			matrix[12] = float(bbox.min().x());
			matrix[13] = float(bbox.min().y());
			matrix[14] = float(bbox.min().z());
			matrix[15] = 1.0f;
			seg_shader->setLocalParamMatrix(2, matrix);

			//set viewport size the same as the texture
			glViewport(0, 0, b->nx(), b->ny());

			//load the texture
			GLint tex_id = -1;
			GLint vd_id = load_brick(0, 0, bricks, i, GL_NEAREST, compression_);
			GLint mask_id = load_brick_mask(bricks, i, GL_NEAREST);
			switch (type)
			{
			case 0:
			case 1:
				tex_id = mask_id;
				break;
			case 2:
				tex_id = vd_id;
				break;
			}

			//size and sample rate
			seg_shader->setLocalParam(4, 1.0/b->nx(), 1.0/b->ny(), 1.0/b->nz(),
				mode_==MODE_OVER?1.0/sampling_rate_:1.0);

			//draw each slice
			for (int z=0; z<b->nz(); z++)
			{
				glFramebufferTexture3D(GL_FRAMEBUFFER, 
					GL_COLOR_ATTACHMENT0,
					GL_TEXTURE_3D,
					tex_id,
					0,
					z);
				draw_slices(double(z+0.5) / double(b->nz()));
			}
		}

		glViewport(vp[0], vp[1], vp[2], vp[3]);

		glMatrixMode(GL_PROJECTION);
		glPopMatrix();
		glMatrixMode(GL_MODELVIEW);
		glPopMatrix();
		////////////////////////////////////////////////////////

		//release 2d mask
		release_texture(6, GL_TEXTURE_2D);
		//release 2d weight map
		if (use_2d)
		{
			release_texture(4, GL_TEXTURE_2D);
			release_texture(5, GL_TEXTURE_2D);
		}

		//release 3d mask
		release_texture((*bricks)[0]->nmask(), GL_TEXTURE_3D);

		// Undo transform.
		glPopMatrix();

		//unbind framebuffer
		glBindFramebuffer(GL_FRAMEBUFFER, cur_framebuffer_id);

		//release seg shader
		if (seg_shader && seg_shader->valid())
			seg_shader->release();

		// Release texture
		release_texture(0, GL_TEXTURE_3D);

		// Reset the blend functions after MIP
		glBlendEquation(GL_FUNC_ADD);
		glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
		glDisable(GL_BLEND);

		//enable depth test
		glEnable(GL_DEPTH_TEST);
	}

	//generate the labeling assuming the mask is already generated
	//type: 0-initialization; 1-maximum intensity filtering
	//mode: 0-normal; 1-posterized; 2-copy values; 3-poster, copy
	void VolumeRenderer::draw_label(int type, int mode, double thresh)
	{
		vector<TextureBrick*> *bricks = tex_->get_bricks();
		if (!bricks || bricks->size() == 0)
			return;

		glActiveTexture(GL_TEXTURE0);
		glEnable(GL_TEXTURE_2D);
		glEnable(GL_TEXTURE_3D);
		//label frame buffer object
		if (!glIsFramebuffer(fbo_label_))
			glGenFramebuffers(1, &fbo_label_);
		GLint cur_framebuffer_id;
		glGetIntegerv(GL_FRAMEBUFFER_BINDING, &cur_framebuffer_id);
		glBindFramebuffer(GL_FRAMEBUFFER, fbo_label_);

		bool has_mask = tex_->nmask()!=-1;

		//--------------------------------------------------------------------------
		// Set up shaders
		//seg shader
		FragmentProgram* seg_shader = 0;

		switch (type)
		{
		case 0://initialize
			seg_shader = seg_shader_factory_.shader(
				LBL_SHDR_INITIALIZE, mode, 0, has_mask,
				false, false, false, hiqual_);
			break;
		case 1://maximum filter
			seg_shader = seg_shader_factory_.shader(
				LBL_SHDR_MIF, mode, 0, has_mask,
				false, false, false, hiqual_);
			break;
		}

		if (seg_shader)
		{
			if (!seg_shader->valid())
				seg_shader->create();
			seg_shader->bind();
		}

		if (type == 0)
		{
			//set uniforms
			//set up shading
			double mat[16];
			glGetDoublev(GL_MODELVIEW_MATRIX, mat);
			Transform mv;
			mv.set_trans(mat);
			Vector light = compute_view().direction();
			light.safe_normalize();
			seg_shader->setLocalParam(0, light.x(), light.y(), light.z(), alpha_);
			if (shading_)
				seg_shader->setLocalParam(1, 2.0 - ambient_, diffuse_, specular_, shine_);
			else
				seg_shader->setLocalParam(1, 2.0 - ambient_, 0.0, specular_, shine_);

			//spacings
			double spcx, spcy, spcz;
			tex_->get_spacings(spcx, spcy, spcz);
			seg_shader->setLocalParam(5, spcx, spcy, spcz, 1.0);

			//transfer function
			seg_shader->setLocalParam(2, inv_?-scalar_scale_:scalar_scale_, gm_scale_, lo_thresh_, hi_thresh_);
			seg_shader->setLocalParam(3, 1.0/gamma3d_, gm_thresh_, offset_, sw_);
			seg_shader->setLocalParam(6, color_.r(), color_.g(), color_.b(), 0.0);
		}

		seg_shader->setLocalParam(7, thresh, 0.0, 0.0, 0.0);

		////////////////////////////////////////////////////////
		// render bricks
		// Set up transform
		glMatrixMode(GL_MODELVIEW);
		glPushMatrix();
		glLoadIdentity();
		glMatrixMode(GL_PROJECTION);
		glPushMatrix();
		glLoadIdentity();
		glDisable(GL_DEPTH_TEST);

		GLint vp[4];
		glGetIntegerv(GL_VIEWPORT, vp);

		for (unsigned int i=0; i < bricks->size(); i++)
		{
			TextureBrick* b = (*bricks)[i];

			switch (type)
			{
			case 0://initialize
				seg_shader->setLocalParam(9, 
					b->nx(), 
					b->ny(), 
					b->nz(), 0.0f);//{nx, ny, nz, 0.0}
				break;
			case 1://maximum filter
				seg_shader->setLocalParam(0, 
					1.0/b->nx(),
					1.0/b->ny(),
					1.0/b->nz(), 0.0f);//{1/spcx, 1/spcy, 1/spcz, 0.0}
				break;
			}

			//set viewport size the same as the texture
			glViewport(0, 0, b->nx(), b->ny());

			//load the texture
			if (type == 0)
				load_brick(0, 0, bricks, i, GL_NEAREST, compression_);
			if (has_mask) load_brick_mask(bricks, i);
			GLuint label_id = load_brick_label(bricks, i);

			//draw each slice
			int z;
			for (z=0; z<b->nz(); z++)
			{
				glFramebufferTexture3D(GL_FRAMEBUFFER, 
					GL_COLOR_ATTACHMENT0,
					GL_TEXTURE_3D,
					label_id,
					0,
					z);

				draw_slices(double(z+0.5) / double(b->nz()));
			}
		}

		glMatrixMode(GL_PROJECTION);
		glPopMatrix();
		glMatrixMode(GL_MODELVIEW);
		glPopMatrix();

		glViewport(vp[0], vp[1], vp[2], vp[3]);
		////////////////////////////////////////////////////////

		//release 3d mask
		release_texture((*bricks)[0]->nmask(), GL_TEXTURE_3D);
		//release 3d label
		release_texture((*bricks)[0]->nlabel(), GL_TEXTURE_3D);

		//release seg shader
		if (seg_shader && seg_shader->valid())
			seg_shader->release();

		//unbind framebuffer
		glBindFramebuffer(GL_FRAMEBUFFER, cur_framebuffer_id);

		// Release texture
		release_texture(0, GL_TEXTURE_3D);

		// Reset the blend functions after MIP
		glBlendEquation(GL_FUNC_ADD);
		glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
		glDisable(GL_BLEND);

		//enable depth test
		glEnable(GL_DEPTH_TEST);
	}

	//calculation
	void VolumeRenderer::calculate(int type, FLIVR::VolumeRenderer *vr_a, FLIVR::VolumeRenderer *vr_b)
	{
		//sync sorting
		Ray view_ray(Point(0.802,0.267,0.534), Vector(0.802,0.267,0.534));
		tex_->set_sort_bricks();
		vector<TextureBrick*> *bricks = tex_->get_sorted_bricks(view_ray);
		if (!bricks || bricks->size() == 0)
			return;
		vector<TextureBrick*> *bricks_a = 0;
		vector<TextureBrick*> *bricks_b = 0;

		bricks_a = vr_a->tex_->get_bricks();
		if (vr_a)
		{
			vr_a->tex_->set_sort_bricks();
			bricks_a = vr_a->tex_->get_sorted_bricks(view_ray);
		}
		if (vr_b)
		{
			vr_b->tex_->set_sort_bricks();
			bricks_b = vr_b->tex_->get_sorted_bricks(view_ray);
		}

		glActiveTexture(GL_TEXTURE0);
		glEnable(GL_TEXTURE_2D);
		glEnable(GL_TEXTURE_3D);
		//mask frame buffer object
		if (!glIsFramebuffer(fbo_label_))
			glGenFramebuffers(1, &fbo_label_);
		GLint cur_framebuffer_id;
		glGetIntegerv(GL_FRAMEBUFFER_BINDING, &cur_framebuffer_id);
		glBindFramebuffer(GL_FRAMEBUFFER, fbo_label_);

		//--------------------------------------------------------------------------
		// Set up shaders
		//calculate shader
		FragmentProgram* cal_shader = cal_shader_factory_.shader(type);

		if (cal_shader)
		{
			if (!cal_shader->valid())
				cal_shader->create();
			cal_shader->bind();
		}

		//set uniforms
		if (type==1 ||
			type==2 ||
			type==3 ||
			type==4 ||
			type==8)
			cal_shader->setLocalParam(0, vr_a?vr_a->get_scalar_scale():1.0,
				vr_b?vr_b->get_scalar_scale():1.0,
				(vr_a&&vr_a->tex_&&vr_a->tex_->nmask()!=-1)?1.0:0.0,
				(vr_b&&vr_b->tex_&&vr_b->tex_->nmask()!=-1)?1.0:0.0);
		else
			cal_shader->setLocalParam(0, vr_a?vr_a->get_scalar_scale():1.0,
				vr_b?vr_b->get_scalar_scale():1.0,
				(vr_a&&vr_a->get_inversion())?-1.0:0.0,
				(vr_b&&vr_b->get_inversion())?-1.0:0.0);
		if (vr_a && (type==6 || type==7))
		{
			cal_shader->setLocalParam(2, inv_?-scalar_scale_:scalar_scale_, gm_scale_, lo_thresh_, hi_thresh_);
			cal_shader->setLocalParam(3, 1.0/gamma3d_, gm_thresh_, offset_, sw_);
		}

		////////////////////////////////////////////////////////
		glMatrixMode(GL_MODELVIEW);
		glPushMatrix();
		glLoadIdentity();
		glMatrixMode(GL_PROJECTION);
		glPushMatrix();
		glLoadIdentity();
		glDisable(GL_DEPTH_TEST);

		for (unsigned int i=0; i < bricks->size(); i++)
		{
			TextureBrick* b = (*bricks)[i];

			//set viewport size the same as the texture
			glViewport(0, 0, b->nx(), b->ny());

			//load the texture
			GLuint tex_id = load_brick(0, 0, bricks, i, GL_NEAREST);
			if (bricks_a) vr_a->load_brick(1, 0, bricks_a, i, GL_NEAREST);
			if (bricks_b) vr_b->load_brick(2, 0, bricks_b, i, GL_NEAREST);
			if ((type==5 || type==6 ||type==7) && bricks_a) vr_a->load_brick_mask(bricks_a, i, GL_NEAREST);
			if (type==8)
			{
				if (bricks_a)
					vr_a->load_brick_mask(bricks_a, i, GL_NEAREST, false, 3);
				if (bricks_b)
					vr_b->load_brick_mask(bricks_b, i, GL_NEAREST, false, 4);
			}

			//draw each slice
			for (int z=0; z<b->nz(); z++)
			{
				glFramebufferTexture3D(GL_FRAMEBUFFER,
					GL_COLOR_ATTACHMENT0,
					GL_TEXTURE_3D,
					tex_id,
					0,
					z);
				draw_slices(double(z+0.5) / double(b->nz()));
			}
		}

		glMatrixMode(GL_PROJECTION);
		glPopMatrix();
		glMatrixMode(GL_MODELVIEW);
		glPopMatrix();
		////////////////////////////////////////////////////////

		//release 3d mask
		release_texture((*bricks)[0]->nmask(), GL_TEXTURE_3D);
		release_texture(4, GL_TEXTURE_3D);
		//release 3d label
		release_texture((*bricks)[0]->nlabel(), GL_TEXTURE_3D);

		//unbind framebuffer
		glBindFramebuffer(GL_FRAMEBUFFER, cur_framebuffer_id);

		//release seg shader
		if (cal_shader && cal_shader->valid())
			cal_shader->release();

		glActiveTexture(GL_TEXTURE0);
		glDisable(GL_TEXTURE_3D);
		glBindTexture(GL_TEXTURE_3D, 0);
		//--------------------------------------------------------------------------

		// Reset the blend functions after MIP
		glBlendEquation(GL_FUNC_ADD);
		glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

		glDisable(GL_BLEND);
	}

	//return the data volume
	void VolumeRenderer::return_volume()
	{
		if (!tex_)
			return;
		vector<TextureBrick*> *bricks = tex_->get_bricks();
		if (!bricks || bricks->size() == 0)
			return;

		int c = 0;
		for (unsigned int i=0; i<bricks->size(); i++)
		{
			load_brick(0, c, bricks, i, GL_NEAREST);
			int nb = (*bricks)[i]->nb(c);
			GLenum format;
			if (nb < 3)
				format = GL_LUMINANCE;
			else
				format = GL_RGBA;

			// download texture data
			int sx = (*bricks)[i]->sx();
			int sy = (*bricks)[i]->sy();
			glPixelStorei(GL_PACK_ROW_LENGTH, sx);
			glPixelStorei(GL_PACK_IMAGE_HEIGHT, sy);
			glPixelStorei(GL_PACK_ALIGNMENT, 1);

			GLenum type = (*bricks)[i]->tex_type(c);
			void* data = (*bricks)[i]->tex_data(c);
			glGetTexImage(GL_TEXTURE_3D, 0, format,
				type, data);

			glPixelStorei(GL_PACK_ROW_LENGTH, 0);
			glPixelStorei(GL_PACK_IMAGE_HEIGHT, 0);
			glPixelStorei(GL_PACK_ALIGNMENT, 4);
		}

		//release 3d texture
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_3D, 0);
		glDisable(GL_TEXTURE_3D);
	}

	//return the mask volume
	void VolumeRenderer::return_mask()
	{
		if (!tex_)
			return;
		vector<TextureBrick*> *bricks = tex_->get_bricks();
		if (!bricks || bricks->size() == 0)
			return;

		int c = tex_->nmask();
		if (c<0 || c>=TEXTURE_MAX_COMPONENTS)
			return;

		for (unsigned int i=0; i<bricks->size(); i++)
		{
			load_brick_mask(bricks, i);
			glActiveTexture(GL_TEXTURE0+c);

			// download texture data
			int sx = (*bricks)[i]->sx();
			int sy = (*bricks)[i]->sy();
			glPixelStorei(GL_PACK_ROW_LENGTH, sx);
			glPixelStorei(GL_PACK_IMAGE_HEIGHT, sy);
			glPixelStorei(GL_PACK_ALIGNMENT, 1);

			GLenum type = (*bricks)[i]->tex_type(c);
			void* data = (*bricks)[i]->tex_data(c);
			glGetTexImage(GL_TEXTURE_3D, 0, GL_LUMINANCE,
				type, data);

			glPixelStorei(GL_PACK_ROW_LENGTH, 0);
			glPixelStorei(GL_PACK_IMAGE_HEIGHT, 0);
			glPixelStorei(GL_PACK_ALIGNMENT, 4);
		}

		//release mask texture
		release_texture(c, GL_TEXTURE_3D);
	}

	//return the label volume
	void VolumeRenderer::return_label()
	{
		if (!tex_)
			return;
		vector<TextureBrick*> *bricks = tex_->get_bricks();
		if (!bricks || bricks->size() == 0)
			return;

		int c = tex_->nlabel();
		if (c<0 || c>=TEXTURE_MAX_COMPONENTS)
			return;

		for (unsigned int i=0; i<bricks->size(); i++)
		{
			load_brick_label(bricks, i);
			glActiveTexture(GL_TEXTURE0+c);

			//download texture data
			glPixelStorei(GL_PACK_ROW_LENGTH, (*bricks)[i]->sx());
			glPixelStorei(GL_PACK_IMAGE_HEIGHT, (*bricks)[i]->sy());
			glPixelStorei(GL_PACK_ALIGNMENT, 4);

			glGetTexImage(GL_TEXTURE_3D, 0, GL_RED_INTEGER,
				(*bricks)[i]->tex_type(c), (*bricks)[i]->tex_data(c));

			glPixelStorei(GL_PACK_ROW_LENGTH, 0);
			glPixelStorei(GL_PACK_IMAGE_HEIGHT, 0);
			//glPixelStorei(GL_PACK_ALIGNMENT, 4);
		}

		//release label texture
		release_texture(c, GL_TEXTURE_3D);
	}

} // namespace FLIVR
