/**
 * Copyright 2013-2023 Software Radio Systems Limited
 *
 * This file is part of srsRAN.
 *
 * srsRAN is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as
 * published by the Free Software Foundation, either version 3 of
 * the License, or (at your option) any later version.
 *
 * srsRAN is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * A copy of the GNU Affero General Public License can be found in
 * the LICENSE file in the top-level directory of this distribution
 * and at http://www.gnu.org/licenses/.
 *
 */

#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>

#include <stdbool.h>

#include "srsran/phy/io/filesink.h"
#include "srsran/phy/rf/rf.h"
#include "srsran/srsran.h"

static bool keep_running = true;
char*       output_file_name;
char*       rf_args = "";
float       rf_gain = 0, rf_freq = -1.0, rf_rate = 0.96e6;
int         nof_samples     = -1;
int         nof_rx_antennas = 1;
uint32_t    max_prb = 6;

void int_handler(int dummy)
{
  keep_running = false;
}

void usage(char* prog)
{
  printf("Usage: %s [agrnv] -f rx_frequency_hz -o output_file\n", prog);
  printf("\t-a RF args [Default %s]\n", rf_args);
  printf("\t-g RF Gain [Default %.2f dB]\n", rf_gain);
  printf("\t-r RF Rate [Default %.6f Hz]\n", rf_rate);
  printf("\t-n nof_samples [Default %d]\n", nof_samples);
  printf("\t-A nof_rx_antennas [Default %d]\n", nof_rx_antennas);
//  printf("\t-p number of PRBs [Default %d]\n", max_prb);
  printf("\t-v srsran_verbose\n");
}

void parse_args(int argc, char** argv)
{
  int opt;
  while ((opt = getopt(argc, argv, "agrnvfoA")) != -1) {
    switch (opt) {
      case 'o':
        output_file_name = argv[optind];
        break;
      case 'a':
        rf_args = argv[optind];
        break;
      case 'g':
        rf_gain = strtof(argv[optind], NULL);
        break;
      case 'r':
        rf_rate = strtof(argv[optind], NULL);
        break;
      case 'f':
        rf_freq = strtof(argv[optind], NULL);
        break;
      case 'n':
        nof_samples = (int)strtol(argv[optind], NULL, 10);
        break;
      case 'A':
        nof_rx_antennas = (int)strtol(argv[optind], NULL, 10);
        break;
//      case 'p':
//        max_prb = (int)strtol(argv[optind], NULL, 10);
      case 'v':
        increase_srsran_verbose_level();
        break;
      default:
        usage(argv[0]);
        exit(-1);
    }
  }
  if (rf_freq < 0) {
    usage(argv[0]);
    exit(-1);
  }
}

static SRSRAN_AGC_CALLBACK(srsran_rf_set_rx_gain_th_wrapper_)
{
  srsran_rf_set_rx_gain_th((srsran_rf_t*)h, gain_db);
}

int srsran_start_agc(srsran_agc_t* agc,
                     SRSRAN_AGC_CALLBACK(set_gain_callback),
                     float min_gain_db,
                     float max_gain_db,
                     float init_gain_value_db,
                     void* uhd_handler)
{
  int n = srsran_agc_init_uhd(agc, SRSRAN_AGC_MODE_PEAK_AMPLITUDE, 0, set_gain_callback, uhd_handler);
  srsran_agc_set_gain_range(agc, min_gain_db, max_gain_db);
  srsran_agc_set_gain(agc, init_gain_value_db);
  return n;
}

int main(int argc, char** argv)
{
  cf_t*             buffer[SRSRAN_MAX_PORTS];
  int               sample_count, n;
  srsran_rf_t       rf;
  srsran_filesink_t sink;
  uint32_t          buflen;
  srsran_agc_t      agc;
//  uint32_t          agc_period = 4;
//  uint32_t fft_size                     = srsran_symbol_sz(max_prb);
//  uint32_t sf_len                       = SRSRAN_SF_LEN(fft_size);
  uint32_t fft_size;
  uint32_t sf_len;

  signal(SIGINT, int_handler);

  parse_args(argc, argv);

  fft_size = srsran_symbol_sz(max_prb);
  sf_len = SRSRAN_SF_LEN(fft_size);

//  buflen       = 4800;
  buflen = sf_len;
  sample_count = 0;

  printf("fft_size=%d, sf_len=%d, buflen=%d", fft_size, sf_len, buflen);

  for (int i = 0; i < nof_rx_antennas; i++) {
    buffer[i] = srsran_vec_cf_malloc(buflen);
    if (!buffer[i]) {
      perror("malloc");
      exit(-1);
    }
  }

  srsran_filesink_init(&sink, output_file_name, SRSRAN_COMPLEX_FLOAT_BIN);

  printf("Opening RF device...");
  if (srsran_rf_open_multi(&rf, rf_args, nof_rx_antennas)) {
    ERROR("Error opening rf");
    exit(-1);
  }

  sigset_t sigset;
  sigemptyset(&sigset);
  sigaddset(&sigset, SIGINT);
  sigprocmask(SIG_UNBLOCK, &sigset, NULL);

  srsran_rf_set_rx_gain(&rf, rf_gain);
  srsran_rf_set_rx_freq(&rf, nof_rx_antennas, rf_freq);
  printf("Set RX freq: %.2f MHz\n", rf_freq / 1000000);
  printf("Set RX gain: %.2f dB\n", rf_gain);
  float srate = srsran_rf_set_rx_srate(&rf, rf_rate);
  if (srate != rf_rate) {
    srate = srsran_rf_set_rx_srate(&rf, rf_rate);
    if (srate != rf_rate) {
      ERROR("Error setting samplign frequency %.2f MHz", rf_rate * 1e-6);
      exit(-1);
    }
  }

  printf("Correctly RX rate: %.2f MHz\n", srate * 1e-6);
  srsran_rf_start_rx_stream(&rf, false);

  srsran_rf_info_t* rf_info = srsran_rf_get_info(&rf);
  srsran_start_agc(&agc, srsran_rf_set_rx_gain_th_wrapper_, rf_info->min_rx_gain, rf_info->max_rx_gain, 0, (void*)&rf);

  while ((sample_count < nof_samples || nof_samples == -1) && keep_running) {
    n = srsran_rf_recv_with_time_multi(&rf, (void**)buffer, buflen, true, NULL, NULL);
    if (n < 0) {
      ERROR("Error receiving samples");
      exit(-1);
    }

//    if (sample_count % 1000000 == 0) {
//      srsran_agc_process(&agc, buffer[0], buflen);
//    }
    srsran_agc_process(&agc, buffer[0], buflen);

    srsran_filesink_write_multi(&sink, (void**)buffer, buflen, nof_rx_antennas);
    sample_count += buflen;
  }



  for (int i = 0; i < nof_rx_antennas; i++) {
    if (buffer[i]) {
      free(buffer[i]);
    }
  }

  srsran_filesink_free(&sink);
  srsran_rf_close(&rf);

  printf("Ok - wrote %d samples\n", sample_count);
  exit(0);
}
