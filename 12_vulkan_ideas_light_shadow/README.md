# Cascaded Shadow Maps and Dynamic Lights (W.I.P.)

Chapter 14 of the book with dynamic rendering, deferred shading, grid shader, SSAO, cascaded shadow maps, and dynamic lights.

**Note:** I removed `VK_IMAGE_LAYOUT_RENDERING_LOCAL_READ` since SSAO needs a blur pass - and such a blur pass is not compatible with the local read stuff.

**Work in progress**

![Example Image with Cascaded Shadow Maps](https://github.com/mich-dy/mastering-game-animations/blob/main/12_vulkan_ideas_light_shadow/image_shadow_maps.png)
![Example Image with Dynamic Lights](https://github.com/mich-dy/mastering-game-animations/blob/main/12_vulkan_ideas_light_shadow/image_dynamic_lights.png)
![Example Image with Dynamic Spotlights](https://github.com/mich-dy/mastering-game-animations/blob/main/12_vulkan_ideas_light_shadow/image_dynamic_spotlights.png)
![Example Image with Dynamic Shadow Maps](https://github.com/mich-dy/mastering-game-animations/blob/main/12_vulkan_ideas_light_shadow/image_dynamic_shadow_maps.png)
