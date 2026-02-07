# Screen Space Ambient Occlusion (SSAO)

Chapter 14 of the book with dynamic rendering, deferred shading, grid shader, and SSAO.

**Note:** I removed `VK_IMAGE_LAYOUT_RENDERING_LOCAL_READ` since SSAO needs a blur pass - and such a blur pass is not compatible with the local read stuff.

![Example Image, SSAO off](https://github.com/mich-dy/mastering-game-animations/blob/main/10_vulkan_ideas_ssao/image_ssao_off.png)
![Example Image, SSAO on](https://github.com/mich-dy/mastering-game-animations/blob/main/10_vulkan_ideas_ssao/image_ssao_on.png)
