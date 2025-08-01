#include "define.hpp"
#include "bmp.hpp"

#include <assert.h>

void canny(hls::FIFO<unsigned char> &input_fifo,
           hls::FIFO<unsigned char> &output_fifo) {
#pragma HLS function top
#pragma HLS function dataflow


#ifndef __SYNTHESIS__
    // For software, the fifo depth has to be larger.
    hls::FIFO<unsigned char> output_fifo_gf(WIDTH * HEIGHT * 2);
    hls::FIFO<unsigned short> output_fifo_sf(WIDTH * HEIGHT * 2);
    hls::FIFO<unsigned char> output_fifo_nm(WIDTH * HEIGHT * 2);
#else
    hls::FIFO<unsigned char> output_fifo_gf(/* depth = */ 2);
    hls::FIFO<unsigned short> output_fifo_sf(/* depth = */ 2);
    hls::FIFO<unsigned char> output_fifo_nm(/* depth = */ 2);
#endif
    gaussian_filter(input_fifo, output_fifo_gf);
    sobel_filter(output_fifo_gf, output_fifo_sf);
    nonmaximum_suppression(output_fifo_sf, output_fifo_nm);
    hysteresis_filter(output_fifo_nm, output_fifo);
}

int main() {
    unsigned int i, j;

    hls::FIFO<unsigned char> input_fifo(/* depth = */ WIDTH * HEIGHT * 2);
    hls::FIFO<unsigned char> output_fifo(/* depth = */ WIDTH * HEIGHT * 2);

    bmp_header_t input_channel_header, golden_output_image_header;
    bmp_pixel_t *input_channel_sw, *input_channel, *golden_output_image,
                *output_image, *output_image_ptr;

    unsigned int matching = 0;

    // read inputs from file
    input_channel = read_bmp(INPUT_IMAGE, &input_channel_header);
    if (!input_channel){
    	printf( "Error: Unable to open the file %s \n",INPUT_IMAGE);
    	return 1;
    }

    input_channel_sw = input_channel;

    golden_output_image = read_bmp(GOLDEN_OUTPUT, &golden_output_image_header);

    output_image = (bmp_pixel_t*)malloc(SIZE * sizeof(bmp_pixel_t));
    output_image_ptr = output_image;

    if (!golden_output_image){
    	printf( "Error: Unable to open the file %s \n",GOLDEN_OUTPUT);
    	return 1;
    }
           
    // convert image to grayscale and write to input array
    unsigned char (*input_image)[WIDTH] = new unsigned char[HEIGHT][WIDTH];
    for (i = 0; i < HEIGHT; i++) {
        for (j = 0; j < WIDTH; j++) {
            unsigned char r = input_channel_sw->r;
            unsigned char g = input_channel_sw->g;
            unsigned char b = input_channel_sw->b;
            unsigned grayscale = (r + g + b) / 3;
            input_image[i][j] = grayscale;
            input_channel_sw++;
        }
    }

    // run software model
    unsigned char (*gaussian_output)[WIDTH] = new unsigned char[HEIGHT][WIDTH];
    hls::ap_uint<10> (*sobel_output)[WIDTH] =
        new hls::ap_uint<10>[HEIGHT][WIDTH];
    unsigned char (*nonmaximum_suppression_output)[WIDTH] =
        new unsigned char[HEIGHT][WIDTH];
    unsigned char (*hysteresis_output_golden)[WIDTH] =
        new unsigned char[HEIGHT][WIDTH];
    gf_sw(input_image, gaussian_output);
    sf_sw(gaussian_output, sobel_output);
    nm_sw(sobel_output, nonmaximum_suppression_output);
    hf_sw(nonmaximum_suppression_output, hysteresis_output_golden);

    // Write input pixels.
    for (i = 0; i < HEIGHT; i++) {
        for (j = 0; j < WIDTH; j++) {
            unsigned char r = input_channel->r;
            unsigned char g = input_channel->g;
            unsigned char b = input_channel->b;
            unsigned grayscale = (r + g + b) / 3;
            input_fifo.write(grayscale);
            input_channel++;
        }
    }

    // Give more inputs to flush out all pixels.
    for (i = 0; i < GF_KERNEL_SIZE * WIDTH + GF_KERNEL_SIZE; i++) {
        input_fifo.write(0);
    }

    canny(input_fifo, output_fifo);

    // output validation
    for (i = 0; i < HEIGHT; i++) {
        for (j = 0; j < WIDTH; j++) {
            unsigned char sw = hysteresis_output_golden[i][j];
            unsigned char gold = golden_output_image->r;
            assert(sw == gold);

            unsigned char hw = output_fifo.read();
            output_image_ptr->r = hw;
            output_image_ptr->g = hw;
            output_image_ptr->b = hw;

            if (sw != hw) {
                printf("ERROR: ");
                printf("i = %d j = %d sw = %d hw = %d\n", i, j, sw, hw);
            } else {
                matching++;
            }

            output_image_ptr++;
            golden_output_image++;
        }
    }

    printf("Result: %d\n", matching);
    bool result_incorrect = 0;
    if (matching == SIZE) {
        printf("RESULT: PASS\n");
    } else {
        printf("RESULT: FAIL\n");
        result_incorrect = 1;
    }

    write_bmp("output.bmp", &input_channel_header, output_image);
    return result_incorrect;
}

