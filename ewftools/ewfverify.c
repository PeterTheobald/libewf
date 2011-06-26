/*
 * ewfverify
 * Verifies the integrity of the media data within the EWF file
 *
 * Copyright (c) 2006-2011, Joachim Metz <jbmetz@users.sourceforge.net>
 *
 * Refer to AUTHORS for acknowledgements.
 *
 * This software is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This software is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this software.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <common.h>
#include <memory.h>
#include <types.h>

#include <libcstring.h>
#include <liberror.h>

#if defined( HAVE_STDLIB_H ) || defined( WINAPI )
#include <stdlib.h>
#endif

#include <libsystem.h>

#include "byte_size_string.h"
#include "digest_hash.h"
#include "ewfcommon.h"
#include "ewfoutput.h"
#include "ewftools_libewf.h"
#include "log_handle.h"
#include "process_status.h"
#include "verification_handle.h"

verification_handle_t *ewfverify_verification_handle = NULL;
int ewfverify_abort                                  = 0;

/* Prints the executable usage information to the stream
 */
void usage_fprint(
      FILE *stream )
{
	if( stream == NULL )
	{
		return;
	}
	fprintf( stream, "Use ewfverify to verify data stored in the EWF format (Expert Witness\n"
	                 "Compression Format).\n\n" );

	fprintf( stream, "Usage: ewfverify [ -A codepage ] [ -d digest_type ] [ -f format ]\n"
	                 "                 [ -l log_filename ] [ -p process_buffer_size ]\n"
	                 "                 [ -hqvVw ] ewf_files\n\n" );

	fprintf( stream, "\tewf_files: the first or the entire set of EWF segment files\n\n" );

	fprintf( stream, "\t-A:        codepage of header section, options: ascii (default),\n"
	                 "\t           windows-874, windows-1250, windows-1251, windows-1252,\n"
	                 "\t           windows-1253, windows-1254, windows-1255, windows-1256,\n"
	                 "\t           windows-1257, windows-1258\n" );
	fprintf( stream, "\t-d:        calculate additional digest (hash) types besides md5,\n"
	                 "\t           options: sha1\n" );
	fprintf( stream, "\t-f:        specify the input format, options: raw (default),\n"
	                 "\t           files (restricted to logical volume files)\n" );
	fprintf( stream, "\t-h:        shows this help\n" );
	fprintf( stream, "\t-l:        logs verification errors and the digest (hash) to the\n"
	                 "\t           log_filename\n" );
	fprintf( stream, "\t-p:        specify the process buffer size (default is the chunk size)\n" );
	fprintf( stream, "\t-q:        quiet shows minimal status information\n" );
	fprintf( stream, "\t-v:        verbose output to stderr\n" );
	fprintf( stream, "\t-V:        print version\n" );
	fprintf( stream, "\t-w:        zero sectors on checksum error (mimic EnCase like behavior)\n" );
}

/* Signal handler for ewfverify
 */
void ewfverify_signal_handler(
      libsystem_signal_t signal LIBSYSTEM_ATTRIBUTE_UNUSED )
{
	liberror_error_t *error = NULL;
	static char *function   = "ewfverify_signal_handler";

	LIBSYSTEM_UNREFERENCED_PARAMETER( signal )

	ewfverify_abort = 1;

	if( ewfverify_verification_handle != NULL )
	{
		if( verification_handle_signal_abort(
		     ewfverify_verification_handle,
		     &error ) != 1 )
		{
			libsystem_notify_printf(
			 "%s: unable to signal verification handle to abort.\n",
			 function );

			libsystem_notify_print_error_backtrace(
			 error );
			liberror_error_free(
			 &error );
		}
	}
	/* Force stdin to close otherwise any function reading it will remain blocked
	 */
	if( libsystem_file_io_close(
	     0 ) != 0 )
	{
		libsystem_notify_printf(
		 "%s: unable to close stdin.\n",
		 function );
	}
}

/* The main program
 */
#if defined( LIBCSTRING_HAVE_WIDE_SYSTEM_CHARACTER )
int wmain( int argc, wchar_t * const argv[] )
#else
int main( int argc, char * const argv[] )
#endif
{
	liberror_error_t *error                                   = NULL;

	libcstring_system_character_t * const *argv_filenames     = NULL;

#if !defined( LIBSYSTEM_HAVE_GLOB )
	libsystem_glob_t *glob                                    = NULL;
#endif

	libcstring_system_character_t *log_filename               = NULL;
	libcstring_system_character_t *program                    = _LIBCSTRING_SYSTEM_STRING( "ewfverify" );
	libcstring_system_character_t *option_format              = NULL;
	libcstring_system_character_t *option_header_codepage     = NULL;
	libcstring_system_character_t *option_process_buffer_size = NULL;

	log_handle_t *log_handle                                  = NULL;

	libcstring_system_integer_t option                        = 0;
	uint8_t calculate_md5                                     = 1;
	uint8_t calculate_sha1                                    = 0;
	uint8_t calculate_sha256                                  = 0;
	uint8_t print_status_information                          = 1;
	uint8_t zero_chunk_on_error                               = 0;
	uint8_t verbose                                           = 0;
	int number_of_filenames                                   = 0;
	int result                                                = 0;
	int status                                                = 0;

	libsystem_notify_set_stream(
	 stderr,
	 NULL );
	libsystem_notify_set_verbose(
	 1 );

	if( libsystem_initialize(
	     "ewftools",
	     &error ) != 1 )
	{
		fprintf(
		 stderr,
		 "Unable to initialize system values.\n" );

		goto on_error;
	}
	ewfoutput_version_fprint(
	 stdout,
	 program );

	while( ( option = libsystem_getopt(
	                   argc,
	                   argv,
	                   _LIBCSTRING_SYSTEM_STRING( "A:d:f:hl:p:qvVw" ) ) ) != (libcstring_system_integer_t) -1 )
	{
		switch( option )
		{
			case (libcstring_system_integer_t) '?':
			default:
				fprintf(
				 stderr,
				 "Invalid argument: %" PRIs_LIBCSTRING_SYSTEM "\n",
				 argv[ optind - 1 ] );

				usage_fprint(
				 stdout );

				goto on_error;

			case (libcstring_system_integer_t) 'A':
				option_header_codepage = optarg;

				break;

			case (libcstring_system_integer_t) 'd':
				if( libcstring_system_string_compare(
				     optarg,
				     _LIBCSTRING_SYSTEM_STRING( "sha1" ),
				     4 ) == 0 )
				{
					calculate_sha1 = 1;
				}
				else if( libcstring_system_string_compare(
				          optarg,
				          _LIBCSTRING_SYSTEM_STRING( "sha256" ),
				          6 ) == 0 )
				{
					calculate_sha256 = 1;
				}
				else
				{
					fprintf(
					 stderr,
					 "Unsupported digest type.\n" );
				}
				break;

			case (libcstring_system_integer_t) 'f':
				option_format = optarg;

				break;

			case (libcstring_system_integer_t) 'h':
				usage_fprint(
				 stdout );

				return( EXIT_SUCCESS );

			case (libcstring_system_integer_t) 'l':
				log_filename = optarg;

				break;

			case (libcstring_system_integer_t) 'p':
				option_process_buffer_size = optarg;

				break;

			case (libcstring_system_integer_t) 'q':
				print_status_information = 0;

				break;

			case (libcstring_system_integer_t) 'v':
				verbose = 1;

				break;

			case (libcstring_system_integer_t) 'V':
				ewfoutput_copyright_fprint(
				 stdout );

				return( EXIT_SUCCESS );

			case (libcstring_system_integer_t) 'w':
				zero_chunk_on_error = 1;

				break;
		}
	}
	if( optind == argc )
	{
		fprintf(
		 stderr,
		 "Missing EWF image file(s).\n" );

		usage_fprint(
		 stdout );

		goto on_error;
	}
	libsystem_notify_set_verbose(
	 verbose );
	libewf_notify_set_verbose(
	 verbose );
	libewf_notify_set_stream(
	 stderr,
	 NULL );

#if !defined( LIBSYSTEM_HAVE_GLOB )
	if( libsystem_glob_initialize(
	     &glob,
	     &error ) != 1 )
	{
		fprintf(
		 stderr,
		 "Unable to initialize glob.\n" );

		goto on_error;
	}
	if( libsystem_glob_resolve(
	     glob,
	     &( argv[ optind ] ),
	     argc - optind,
	     &error ) != 1 )
	{
		fprintf(
		 stderr,
		 "Unable to resolve glob.\n" );

		goto on_error;
	}
	argv_filenames      = glob->result;
	number_of_filenames = glob->number_of_results;
#else
	argv_filenames      = &( argv[ optind ] );
	number_of_filenames = argc - optind;
#endif

	if( verification_handle_initialize(
	     &ewfverify_verification_handle,
	     calculate_md5,
	     calculate_sha1,
	     calculate_sha256,
	     &error ) != 1 )
	{
		fprintf(
		 stderr,
		 "Unable to create verification handle.\n" );

		goto on_error;
	}
	if( libsystem_signal_attach(
	     ewfverify_signal_handler,
	     &error ) != 1 )
	{
		fprintf(
		 stderr,
		 "Unable to attach signal handler.\n" );

		libsystem_notify_print_error_backtrace(
		 error );
		liberror_error_free(
		 &error );
	}
	result = verification_handle_open_input(
	          ewfverify_verification_handle,
	          argv_filenames,
	          number_of_filenames,
	          &error );

	if( ewfverify_abort != 0 )
	{
		goto on_abort;
	}
	if( result != 1 )
	{
		fprintf(
		 stderr,
		 "Unable to open EWF image file(s).\n" );

		goto on_error;
	}
#if !defined( LIBSYSTEM_HAVE_GLOB )
	if( libsystem_glob_free(
	     &glob,
	     &error ) != 1 )
	{
		fprintf(
		 stderr,
		 "Unable to free glob.\n" );

		goto on_error;
	}
#endif
	if( option_header_codepage != NULL )
	{
		result = verification_handle_set_header_codepage(
			  ewfverify_verification_handle,
		          option_header_codepage,
			  &error );

		if( result == -1 )
		{
			fprintf(
			 stderr,
			 "Unable to set header codepage.\n" );

			goto on_error;
		}
		else if( result == 0 )
		{
			fprintf(
			 stderr,
			 "Unsupported header codepage defaulting to: ascii.\n" );
		}
	}
	if( option_format != NULL )
	{
		result = verification_handle_set_format(
			  ewfverify_verification_handle,
			  option_format,
			  &error );

		if( result == -1 )
		{
			fprintf(
			 stderr,
			 "Unable to set format.\n" );

			goto on_error;
		}
		else if( result == 0 )
		{
			fprintf(
			 stderr,
			 "Unsupported input format defaulting to: raw.\n" );
		}
	}
	if( verification_handle_set_zero_chunk_on_error(
	     ewfverify_verification_handle,
	     zero_chunk_on_error,
	     &error ) != 1 )
	{
		fprintf(
		 stderr,
		 "Unable to set zero on chunk error.\n" );

		goto on_error;
	}
	if( option_process_buffer_size != NULL )
	{
		result = verification_handle_set_process_buffer_size(
			  ewfverify_verification_handle,
			  option_process_buffer_size,
			  &error );

		if( result == -1 )
		{
			fprintf(
			 stderr,
			 "Unable to set process buffer size.\n" );

			goto on_error;
		}
		else if( ( result == 0 )
		      || ( ewfverify_verification_handle->process_buffer_size > (size_t) SSIZE_MAX ) )
		{
			ewfverify_verification_handle->process_buffer_size = 0;

			fprintf(
			 stderr,
			 "Unsupported process buffer size defaulting to: chunk size.\n" );
		}
	}
	if( log_filename != NULL )
	{
		if( log_handle_initialize(
		     &log_handle,
		     &error ) != 1 )
		{
			fprintf(
			 stderr,
			 "Unable to create log handle.\n" );

			goto on_error;
		}
		if( log_handle_open(
		     log_handle,
		     log_filename,
		     &error ) != 1 )
		{
			fprintf(
			 stderr,
			 "Unable to open log file: %" PRIs_LIBCSTRING_SYSTEM ".\n",
			 log_filename );

			goto on_error;
		}
	}
	if( ewfverify_verification_handle->input_format == VERIFICATION_HANDLE_INPUT_FORMAT_FILES )
	{
		result = verification_handle_verify_single_files(
		          ewfverify_verification_handle,
		          print_status_information,
		          log_handle,
		          &error );

		if( result != 1 )
		{
			fprintf(
			 stderr,
			 "Unable to verify single files.\n" );

			libsystem_notify_print_error_backtrace(
			 error );
			liberror_error_free(
			 &error );
		}
	}
	else
	{
		result = verification_handle_verify_input(
		          ewfverify_verification_handle,
		          print_status_information,
		          log_handle,
		          &error );

		if( result != 1 )
		{
			fprintf(
			 stderr,
			 "Unable to verify input.\n" );

			libsystem_notify_print_error_backtrace(
			 error );
			liberror_error_free(
			 &error );
		}
	}
	if( result != 1 )
	{
		status = PROCESS_STATUS_FAILED;
	}
	else
	{
		status = PROCESS_STATUS_COMPLETED;
	}
	if( log_handle != NULL )
	{
		if( log_handle_close(
		     log_handle,
		     &error ) != 0 )
		{
			fprintf(
			 stderr,
			 "Unable to close log handle.\n" );

			goto on_error;
		}
		if( log_handle_free(
		     &log_handle,
		     &error ) != 1 )
		{
			fprintf(
			 stderr,
			 "Unable to free log handle.\n" );

			goto on_error;
		}
	}
on_abort:
	if( libsystem_signal_detach(
	     &error ) != 1 )
	{
		fprintf(
		 stderr,
		 "Unable to detach signal handler.\n" );

		libsystem_notify_print_error_backtrace(
		 error );
		liberror_error_free(
		 &error );
	}
	if( verification_handle_close(
	     ewfverify_verification_handle,
	     &error ) != 0 )
	{
		fprintf(
		 stderr,
		 "Unable to close EWF file(s).\n" );

		goto on_error;

	}
	if( verification_handle_free(
	     &ewfverify_verification_handle,
	     &error ) != 1 )
	{
		fprintf(
		 stderr,
		 "Unable to free verification handle.\n" );

		goto on_error;
	}
	if( ewfverify_abort != 0 )
	{
		fprintf(
		 stdout,
		 "%" PRIs_LIBCSTRING_SYSTEM ": ABORTED\n",
		 program );

		return( EXIT_FAILURE );
	}
	if( status != PROCESS_STATUS_COMPLETED )
	{
		fprintf(
		 stdout,
		 "%" PRIs_LIBCSTRING_SYSTEM ": FAILURE\n",
		 program );

		return( EXIT_FAILURE );
	}
	fprintf(
	 stdout,
	 "%" PRIs_LIBCSTRING_SYSTEM ": SUCCESS\n",
	 program );

	return( EXIT_SUCCESS );

on_error:
	if( error != NULL )
	{
		libsystem_notify_print_error_backtrace(
		 error );
		liberror_error_free(
		 &error );
	}
	if( log_handle != NULL )
	{
		log_handle_close(
		 log_handle,
		 NULL );
		log_handle_free(
		 &log_handle,
		 NULL );
	}
	if( ewfverify_verification_handle != NULL )
	{
		verification_handle_close(
		 ewfverify_verification_handle,
		 NULL );
		verification_handle_free(
		 &ewfverify_verification_handle,
		 NULL );
	}
#if !defined( LIBSYSTEM_HAVE_GLOB )
	if( glob != NULL )
	{
		libsystem_glob_free(
		 &glob,
		 NULL );
	}
#endif
	return( EXIT_FAILURE );
}

