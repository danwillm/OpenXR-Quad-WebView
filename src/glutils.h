#pragma once

#include <array>
#include <string>
#include <vector>
#include <unordered_map>

#include <GLES3/gl3.h>
#include <GLES2/gl2ext.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>

#include "glm/glm.hpp"
#include <mutex>

class Texture
{
public:
	Texture( bool external,
			 GLuint externalHandle,
			 bool oes,
			 uint32_t width = 0,
			 uint32_t height = 0,
			 GLint internalFormat = GL_SRGB8_ALPHA8,
			 GLenum format = GL_RGBA,
			 const std::vector<uint8_t> &content = {} );

	uint32_t GetWidth() const
	{
		return m_unWidth;
	}

	uint32_t GetHeight() const
	{
		return m_unHeight;
	}

	GLuint GetGLTexture() const
	{
		return m_glTexture;
	}

	GLenum GetTarget() const
	{
		return m_glTarget;
	}

	bool IsOES() const
	{
		return m_bIsOES;
	}

	~Texture();

private:
	bool m_bIsOES;
	uint32_t m_unWidth, m_unHeight;
	GLuint m_glTexture;
	GLenum m_glTarget;
	bool m_bIsExternal{};
};

class Shader
{
public:
	Shader( const std::string &vertexShader, const std::string &fragmentShader );

	void BindAttribLocation( const GLuint index, const std::string &name );

	void BindVertexArray( const GLuint vao );

	void LinkShader();

	void BindShader() const;
	int CheckReload(); // Must happen on render thread.  If <0, error.  If >1 reload.  If == 0, no effect.
	bool BShaderIsOk() const
	{
		return m_bShaderIsOk;
	}

	void SetUniform1i( const std::string &name, const int value );

	void SetUniformL1f( int id, const float value );

	void SetUniformL1i( int id, const int value );

	void SetUniform1f( const std::string &name, const float value );

	void SetUniform2f( const std::string &name, const float value1, const float value2 );

	void SetUniformLVec3( int id, const float *fv );

	void SetUniformLVec4( int id, const float *fv );

	void SetUniformVec4( const std::string &name, const glm::vec4 &vec4 );

	void SetUniformLVec2( int id, const glm::vec2 &vec2 );

	void SetUniformVec2( const std::string &name, const glm::vec2 &vec2 );

	void SetUniformLMat4( int id, const glm::mat4 &mat4 );

	void SetUniformMat4( const std::string &name, const glm::mat4 &mat4 );

	void UnbindShader();

	void ReloadWhenReady( const std::string & sShaderVert, const std::string & sShaderFrag );

	~Shader();

private:
	GLint GetUniformLocation( const std::string &name );

	std::unordered_map<std::string, GLint> m_uniformLocations{};

	GLuint m_vertexShader = 0;
	GLuint m_fragmentShader = 0;
	GLuint m_program = 0;
	bool m_bShaderIsOk = false;

	std::mutex mutReload;
	std::string sReloadVert;
	std::string sReloadFrag;
};


class RenderState
{
public:
	RenderState( const Texture *renderTarget );

	GLuint GetFrameBuffer() const
	{
		return m_frameBuffer;
	}

	const Texture *GetRenderTarget() const
	{
		return m_renderTarget;
	}

	~RenderState();

private:
	const Texture *m_renderTarget;
	GLuint m_frameBuffer = 0;
};


class FrameBuffer
{
public:
	FrameBuffer();

	void Bind() const;

	void BindFramebufferWithTexture( GLuint texture ) const;

	void Unbind() const;

	~FrameBuffer();

private:
	GLuint m_frameBuffer = 0;
};

struct VertexAttribute
{
	GLuint unIndex = 0;
	GLint nSize = 0;
	GLenum eType = 0;
	GLboolean bNormalized = false;
	GLsizei nStride = 0;
	const GLvoid *pPointer = nullptr;
};

struct Geometry
{
	GLuint unVertexBuffer = 0;
	GLuint unUvBuffer = 0;
	GLuint unIndexBuffer = 0;
	GLuint unVertexArrayObject = 0;
	int nVertexCount = 0;
	int nIndexCount = 0;
	std::vector<VertexAttribute> vecVertexAttributes = {};

	~Geometry();
};

struct PanelConfig
{
	float fWidthMeters;
	float fHeightMeters;

	uint32_t unTextureWidth;
	uint32_t unTextureHeight;

	float fRefreshRate = 60.f;
};

class PanelRenderer
{
public:
	PanelRenderer( PanelConfig config );

	void
	RenderToTexture( const glm::mat4 &view, const glm::mat4 &projection, const std::unique_ptr<Texture> &panelTexture,
					 const GLuint renderTexture, const int renderWidth, const int renderHeight );

	void RenderToScreen( const std::unique_ptr<Texture> &panelTexture, const int renderWidth, const int renderHeight );

	const PanelConfig &GetPanelConfig();

private:
	PanelConfig m_panelConfig;

	Geometry m_panelGeometry;

	std::unique_ptr<Shader> m_pShader;

	std::unique_ptr<FrameBuffer> m_pFramebuffer;
};

void SwapBuffers();