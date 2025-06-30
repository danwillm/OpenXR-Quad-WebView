#include "glutils.h"

#include <EGL/egl.h>
#include <cinttypes>
#include <map>

#include "check.h"

extern EGLDisplay egl_display;
extern EGLSurface egl_surface;
extern EGLContext egl_context;
extern EGLConfig egl_config;

static PFNGLTEXSTORAGE2DPROC glTexStorage2DProc = nullptr;

Texture::Texture( bool external,
				  GLuint externalHandle,
				  bool oes,
				  uint32_t width,
				  uint32_t height,
				  GLint internalFormat,
				  GLenum format,
				  const std::vector<uint8_t> &content )
{

	glTexStorage2DProc = (PFNGLTEXSTORAGE2DPROC) eglGetProcAddress( "glTexStorage2D" );
	m_bIsOES = oes;
	m_unWidth = width;
	m_unHeight = height;
	m_glTarget = oes ? GL_TEXTURE_EXTERNAL_OES : GL_TEXTURE_2D;

	m_bIsExternal = external;

	if ( m_bIsExternal )
	{
		m_glTexture = externalHandle;
	}
	else
	{
		GL_CHECK( glGenTextures( 1, &m_glTexture ));
	}

	GL_CHECK( glBindTexture( m_glTarget, m_glTexture ));

	if ( format == GL_RGB )
	{
		Log( "[GLUtils] Unpack alignment set to 1" );
		GL_CHECK( glPixelStorei( GL_UNPACK_ALIGNMENT, 1 ));
	}


	if ( !oes && !external && width != 0 && height != 0 )
	{
		if ( !content.empty())
		{
			Log( "[GLUtils] Texture content size: %" PRIu64 , content.size() );
			GL_CHECK( glTexImage2D( m_glTarget,
									0,
									internalFormat,
									width,
									height,
									0,
									format,
									GL_UNSIGNED_BYTE,
									content.data()));
		}
		else
		{
			GL_CHECK( glTexStorage2DProc( m_glTarget, 1, internalFormat, width, height ));
		}
	}

	GL_CHECK( glTexParameteri( m_glTarget, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE ));
	GL_CHECK( glTexParameteri( m_glTarget, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE ));
	GL_CHECK( glTexParameteri( m_glTarget, GL_TEXTURE_MAG_FILTER, GL_LINEAR ));
	GL_CHECK( glTexParameteri( m_glTarget, GL_TEXTURE_MIN_FILTER, GL_LINEAR ));
}

Texture::~Texture()
{
	if ( !m_bIsExternal )
	{
		GL_CHECK( glDeleteTextures( 1, &m_glTexture ));
	}
}

static GLuint CreateShader( GLenum type, const std::string &sShader )
{
	GLuint shader = glCreateShader( type );

	const char *cstrShader = sShader.c_str();
	GL_CHECK( glShaderSource( shader, 1, &cstrShader, nullptr ));

	GLint result = 0;
	int info_length = 0;
	GL_CHECK( glCompileShader( shader ));
	GL_CHECK( glGetShaderiv( shader, GL_COMPILE_STATUS, &result ));
	glGetShaderiv( shader, GL_INFO_LOG_LENGTH, &info_length );

	if( result != GL_TRUE )
	{
		Log( LogWarning, "[GLUtils] Shader compilation failed. Length of Error: %d\n", info_length );
	}
	if ( info_length > 0 )
	{
		std::vector<char> shaderErrorMessage( info_length + 1 );
		glGetShaderInfoLog( shader, info_length, nullptr, &shaderErrorMessage[ 0 ] );

		Log( LogWarning, "[GLUtils] Failed to compile shader %s", &shaderErrorMessage[ 0 ] );
	}

	return shader;
}

Shader::Shader( const std::string &vertexShader, const std::string &fragmentShader )
{
	m_vertexShader = CreateShader( GL_VERTEX_SHADER, vertexShader );
	m_fragmentShader = CreateShader( GL_FRAGMENT_SHADER, fragmentShader );

	m_program = glCreateProgram();
	GL_CHECK( glAttachShader( m_program, m_vertexShader ));
	GL_CHECK( glAttachShader( m_program, m_fragmentShader ));
}

void Shader::BindAttribLocation( const GLuint index, const std::string &name )
{
	GL_CHECK( glBindAttribLocation( m_program, index, name.c_str()));
}

void Shader::LinkShader()
{
	GL_CHECK( glLinkProgram( m_program ));

	GLint result = 0;
	int info_length = 0;
	GL_CHECK( glGetProgramiv( m_program, GL_LINK_STATUS, &result ));
	glGetShaderiv( m_program, GL_INFO_LOG_LENGTH, &info_length );

	if( result != GL_TRUE )
	{
		Log( LogWarning, "[GLUtils] Failed to link shader. Got log length %d", info_length );
		m_bShaderIsOk = false;
	}
	else
	{
		m_bShaderIsOk = true;
	}
	if ( info_length > 0 )
	{
		std::vector<char> shaderErrorMessage( info_length + 1 );
		glGetProgramInfoLog( m_program, info_length, nullptr, &shaderErrorMessage[ 0 ] );

		Log( LogWarning, "[GLUtils] Failed to link shader (%d): %s", info_length, &shaderErrorMessage[ 0 ] );
	}


	GL_CHECK( glDetachShader( m_program, m_vertexShader ));
	GL_CHECK( glDetachShader( m_program, m_fragmentShader ));

	GL_CHECK( glDeleteShader( m_vertexShader ));
	GL_CHECK( glDeleteShader( m_fragmentShader ));
}

void Shader::BindShader() const
{
	glGetError();
	GL_CHECK( glUseProgram( m_program ));
}


int Shader::CheckReload()
{
	std::lock_guard<std::mutex> guard(mutReload);
	if( !sReloadVert.empty() && !sReloadFrag.empty() )
	{
		GL_CHECK( glDeleteProgram( m_program ));

		m_vertexShader = CreateShader( GL_VERTEX_SHADER, sReloadVert );
		m_fragmentShader = CreateShader( GL_FRAGMENT_SHADER, sReloadFrag );

		sReloadVert = "";
		sReloadFrag = "";
		m_program = glCreateProgram();
		GL_CHECK( glAttachShader( m_program, m_vertexShader ));
		GL_CHECK( glAttachShader( m_program, m_fragmentShader ));
		LinkShader();
		return m_bShaderIsOk ? 1 : -1;
	}
	return 0;
}

void Shader::BindVertexArray( const GLuint vao )
{
	GL_CHECK( glBindVertexArray( vao ));
}

void Shader::SetUniform1i( const std::string &name, const int value )
{
	GLint location = GetUniformLocation( name );
	GL_CHECK( glUniform1i( location, value ));
}

void Shader::SetUniformL1f( int id, const float value )
{
	GL_CHECK( glUniform1f( id, value ) );
}

void Shader::SetUniformL1i( int id, const int value )
{
	GL_CHECK( glUniform1i( id, value ) );
}

void Shader::SetUniform1f( const std::string &name, const float value )
{
	SetUniformL1f( GetUniformLocation( name ), value );
}

void Shader::SetUniform2f( const std::string &name, const float x, const float y )
{
	GL_CHECK( glUniform2f( GetUniformLocation( name ), x, y ));
}

void Shader::SetUniformLVec2( int id, const glm::vec2 &vec2 )
{
	GL_CHECK( glUniform2f( id, vec2.x, vec2.y ));
}

void Shader::SetUniformVec2( const std::string &name, const glm::vec2 &vec2 )
{
	SetUniformLVec2( GetUniformLocation( name ), vec2 );
}

void Shader::SetUniformVec4( const std::string &name, const glm::vec4 &vec4 )
{
	GL_CHECK( glUniform4f( GetUniformLocation( name ), vec4.x, vec4.y, vec4.z, vec4.w ));
}

void Shader::SetUniformLMat4( int id, const glm::mat4 &mat4 )
{
	GL_CHECK( glUniformMatrix4fv( id, 1, GL_FALSE, &mat4[ 0 ][ 0 ] ));
}

void Shader::SetUniformMat4( const std::string &name, const glm::mat4 &mat4 )
{
	SetUniformLMat4( GetUniformLocation( name ), mat4 );
}

void Shader::SetUniformLVec3( int id, const float *fv )
{
	glUniform3fv( id, 1, fv );
}

void Shader::SetUniformLVec4( int id, const float *fv )
{
	glUniform4fv( id, 1, fv );
}

void Shader::UnbindShader()
{
	GL_CHECK( glUseProgram( 0 ));
}

void Shader::ReloadWhenReady( const std::string & sShaderVert, const std::string & sShaderFrag )
{
	std::lock_guard<std::mutex> guard(mutReload);
	sReloadVert = sShaderVert;
	sReloadFrag = sShaderFrag;
}

GLint Shader::GetUniformLocation( const std::string &name )
{
	auto it = m_uniformLocations.find( name );
	if ( it != m_uniformLocations.end())
	{
		return it->second;
	}

	GL_CHECK( GLint location = glGetUniformLocation( m_program, name.c_str()));
	if ( location == -1 )
	{
		Log( LogWarning, "[GLUtils] Failed to find uniform: %s", name.c_str());
	}

	m_uniformLocations[ name ] = location;

	return location;
}

Shader::~Shader()
{
	GL_CHECK( glDeleteProgram( m_program ));
}

RenderState::RenderState( const Texture *renderTarget )
{
	m_renderTarget = renderTarget;

	GL_CHECK( glGenFramebuffers( 1, &m_frameBuffer ));
	GL_CHECK( glBindFramebuffer( GL_DRAW_FRAMEBUFFER, m_frameBuffer ));
	GL_CHECK( glFramebufferTexture2D( GL_DRAW_FRAMEBUFFER,
									  GL_COLOR_ATTACHMENT0,
									  m_renderTarget->GetTarget(),
									  renderTarget->GetGLTexture(),
									  0 ));
}

RenderState::~RenderState()
{
	GL_CHECK( glDeleteFramebuffers( 1, &m_frameBuffer ));
}

FrameBuffer::FrameBuffer()
{
	GL_CHECK( glGenFramebuffers( 1, &m_frameBuffer ));
}

void FrameBuffer::Bind() const
{
	GL_CHECK( glBindFramebuffer( GL_FRAMEBUFFER, m_frameBuffer ));
}

void FrameBuffer::BindFramebufferWithTexture( GLuint texture ) const
{
	GL_CHECK( glBindFramebuffer( GL_FRAMEBUFFER, m_frameBuffer ));
	GL_CHECK( glFramebufferTexture2D( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0 ));
}

void FrameBuffer::Unbind() const
{
	GL_CHECK( glBindFramebuffer( GL_FRAMEBUFFER, 0 ));
}

FrameBuffer::~FrameBuffer()
{
	GL_CHECK( glDeleteFramebuffers( 1, &m_frameBuffer ));
}

Geometry::~Geometry()
{
	GL_CHECK( glDeleteBuffers( 1, &unVertexBuffer ));
	GL_CHECK( glDeleteBuffers( 1, &unIndexBuffer ));
	GL_CHECK( glDeleteBuffers( 1, &unUvBuffer ));

	GL_CHECK( glDeleteVertexArrays( 1, &unVertexArrayObject ));
}

enum VertexAttributeLocation
{
	VERTEX_ATTRIBUTE_LOCATION_POSITION = 0,
	VERTEX_ATTRIBUTE_LOCATION_UV = 1,
};

std::map<VertexAttributeLocation, std::string> kPanelVertexAttributeLocations = {
		{
				VERTEX_ATTRIBUTE_LOCATION_POSITION, "vertexPosition",
		},
		{
				VERTEX_ATTRIBUTE_LOCATION_UV,       "vertexUv",
		}
};

const std::string PANEL_VERTEX_SHADER = R"glsl(#version 300 es
        layout (location = 0) in vec3 vertexPosition;
        in vec2 vertexUv;

        out vec2 texCoord;
        void main() {
            gl_Position = vec4(vertexPosition, 1.0);
            texCoord = vertexUv;
        }
    )glsl";

static const std::string PANEL_FRAGMENT_SHADER = R"glsl(#version 300 es
        uniform sampler2D texture;
        in vec2 texCoord;

        out vec4 out_FragColor;
        void main()
        {
            out_FragColor = texture(texture, texCoord);
        }
)glsl";

PanelRenderer::PanelRenderer( PanelConfig config ) : m_panelConfig( config )
{
	m_pFramebuffer = std::make_unique<FrameBuffer>();

	m_pShader = std::make_unique<Shader>( PANEL_VERTEX_SHADER, PANEL_FRAGMENT_SHADER );
	for ( const auto &[location, name]: kPanelVertexAttributeLocations )
	{
		m_pShader->BindAttribLocation( location, name );
	}
	m_pShader->LinkShader();

	m_panelGeometry.nVertexCount = 6;
	m_panelGeometry.nIndexCount = 0;

	VertexAttribute positionAttribute = {
			.unIndex = VERTEX_ATTRIBUTE_LOCATION_POSITION,
			.nSize = 3,
			.eType = GL_FLOAT,
			.bNormalized = GL_FALSE,
			.nStride = 5 * sizeof( float ),
			.pPointer = (void *) 0,
	};

	VertexAttribute uvAttribute = {
			.unIndex = VERTEX_ATTRIBUTE_LOCATION_UV,
			.nSize = 2,
			.eType = GL_FLOAT,
			.bNormalized = GL_FALSE,
			.nStride = 5 * sizeof( float ),
			.pPointer = (void *) (3 * sizeof( float )),
	};

	m_panelGeometry.vecVertexAttributes.emplace_back( positionAttribute );
	m_panelGeometry.vecVertexAttributes.emplace_back( uvAttribute );

	GL_CHECK( glGenBuffers( 1, &m_panelGeometry.unVertexBuffer ));
	GL_CHECK( glBindBuffer( GL_ARRAY_BUFFER, m_panelGeometry.unVertexBuffer ));

	const GLfloat vertexBufferData[] = {
			-1.f, -1.f, 0.f, 0.f, 1.f, // x, y, z, u, v
			1.f, -1.f, 0.f, 1.f, 1.f,
			-1.f, 1.f, 0.f, 0.f, 0.f,
			1.f, -1.f, 0.f, 1.f, 1.f,
			1.f, 1.f, 0.f, 1.f, 0.f,
			-1.f, 1.f, 0.f, 0.f, 0.f,
	};
	GL_CHECK( glBufferData( GL_ARRAY_BUFFER, 6 * 5 * sizeof( float ), vertexBufferData, GL_STATIC_DRAW ));
	GL_CHECK( glBindBuffer( GL_ARRAY_BUFFER, 0 ));


	GL_CHECK( glGenVertexArrays( 1, &m_panelGeometry.unVertexArrayObject ));
	GL_CHECK( glBindVertexArray( m_panelGeometry.unVertexArrayObject ));

	GL_CHECK( glBindBuffer( GL_ARRAY_BUFFER, m_panelGeometry.unVertexBuffer ));
	for ( const VertexAttribute &attribute: m_panelGeometry.vecVertexAttributes )
	{
		GL_CHECK( glEnableVertexAttribArray( attribute.unIndex ));
		GL_CHECK( glVertexAttribPointer( attribute.unIndex,
										 attribute.nSize,
										 attribute.eType,
										 attribute.bNormalized,
										 attribute.nStride,
										 attribute.pPointer ));
	}

	GL_CHECK( glBindBuffer( GL_ARRAY_BUFFER, 0 ));
}

void
PanelRenderer::RenderToTexture( const glm::mat4 &view, const glm::mat4 &projection,
								const std::unique_ptr<Texture> &panelTexture,
								const GLuint renderTexture, const int renderWidth, const int renderHeight )
{
	m_pFramebuffer->BindFramebufferWithTexture( renderTexture );

	GL_CHECK( glViewport( 0, 0, renderWidth, renderHeight ));
	GL_CHECK( glClearColor( 0.f, 0.f, 0.f, 0.f ));
	GL_CHECK( glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT ));

	m_pShader->BindShader();
	m_pShader->BindVertexArray( m_panelGeometry.unVertexArrayObject );

	GL_CHECK( glActiveTexture( GL_TEXTURE0 ));
	GL_CHECK( glBindTexture( GL_TEXTURE_2D, panelTexture->GetGLTexture()));

	GL_CHECK( glBindVertexArray( m_panelGeometry.unVertexArrayObject ));
	GL_CHECK( glDrawArrays( GL_TRIANGLES, 0, m_panelGeometry.nVertexCount ));

	m_pFramebuffer->Unbind();
}

void PanelRenderer::RenderToScreen( const std::unique_ptr<Texture> &panelTexture, const int renderWidth,
									const int renderHeight )
{
	GL_CHECK( glBindFramebuffer( GL_DRAW_FRAMEBUFFER, 0 ));

	GL_CHECK( glViewport( 0, 0, renderWidth, renderHeight ));
	GL_CHECK( glClearColor( 0.f, 0.f, 0.f, 0.f ));
	GL_CHECK( glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT ));

	m_pShader->BindShader();
	m_pShader->BindVertexArray( m_panelGeometry.unVertexArrayObject );

	GL_CHECK( glActiveTexture( GL_TEXTURE0 ));
	GL_CHECK( glBindTexture( GL_TEXTURE_2D, panelTexture->GetGLTexture()));

	GL_CHECK( glBindVertexArray( m_panelGeometry.unVertexArrayObject ));
	GL_CHECK( glDrawArrays( GL_TRIANGLES, 0, m_panelGeometry.nVertexCount ));

	GL_CHECK( glBindFramebuffer( GL_FRAMEBUFFER, 0 ));
}

const PanelConfig &PanelRenderer::GetPanelConfig()
{
	return m_panelConfig;
}

void SwapBuffers()
{
	eglSwapBuffers( egl_display, egl_surface );
}