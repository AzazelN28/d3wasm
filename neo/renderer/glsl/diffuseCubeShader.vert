#version 100
precision mediump float;
  
// In
attribute lowp vec4 attr_Color;
attribute vec3 attr_Normal;
attribute highp vec4 attr_Vertex;
  
// Uniforms
uniform highp mat4 u_modelViewProjectionMatrix;
uniform mat4 u_textureMatrix;
uniform lowp float u_colorAdd;
uniform lowp float u_colorModulate;
  
// Out
// gl_Position
varying vec4 var_TexCoord;
varying lowp vec4 var_Color;
  
void main(void)
{
  var_TexCoord = u_textureMatrix * vec4(attr_Normal, 0.0);
  
  var_Color = attr_Color * u_colorModulate + vec4(u_colorAdd, u_colorAdd, u_colorAdd, u_colorAdd);
  
  gl_Position = u_modelViewProjectionMatrix * attr_Vertex;
}
