// A slow ripple across the menu painting, as if a wind were crossing it.
//
// On the flat artwork this does most of the work: the long white hair and the
// red ribbons pick the distortion up and appear to lift, so a single still
// image reads as moving without any layer being cut out of it.
//
// Two rules keep it from looking like a fault rather than weather:
//
//   * It is confined to the right of the frame. An earlier version scaled the
//     displacement by coord.x, which grows steadily from the left edge - enough
//     that the title lettering over the left column visibly shimmered. The
//     smoothstep holds the left 40% completely still, which is where every
//     piece of menu text sits.
//
//   * The amplitude stays tiny. Past roughly 0.004 of the texture the moon's
//     rim and the skyline start to bend, and a wobbling horizon reads as a
//     rendering bug.

uniform sampler2D texture;
uniform float time;

void main()
{
    vec2 coord = gl_TexCoord[0].xy;

    // Nothing on the left, full strength out at the hair and the ribbons.
    float wind = smoothstep(0.40, 1.0, coord.x);

    // Two frequencies per axis so the loop never repeats visibly the way a
    // single sine does.
    float waveX = (sin(coord.y * 15.0 + time * 2.6) * 0.0026
                 + sin(coord.y * 31.0 - time * 1.3) * 0.0009) * wind;
    float waveY =  cos(coord.x * 10.0 + time * 1.8) * 0.0016 * wind;

    gl_FragColor = gl_Color * texture2D(texture, coord + vec2(waveX, waveY));
}
