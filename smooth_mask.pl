#!/usr/local/bin/perl5 -w

#---------------------------------------------------------------------------
#@COPYRIGHT :
#             Copyright 1997, Alex P. Zijdenbos
#             McConnell Brain Imaging Centre,
#             Montreal Neurological Institute, McGill University.
#             Permission to use, copy, modify, and distribute this
#             software and its documentation for any purpose and without
#             fee is hereby granted, provided that the above copyright
#             notice appear in all copies.  The author and McGill University
#             make no representations about the suitability of this
#             software for any purpose.  It is provided "as is" without
#             express or implied warranty.
#---------------------------------------------------------------------------- 
#$RCSfile: smooth_mask.pl,v $
#$Revision: 1.1 $
#$Author: bert $
#$Date: 2005-05-25 21:23:13 $
#$State: Exp $
#---------------------------------------------------------------------------

require "ctime.pl";
require "file_utilities.pl";
require "path_utilities.pl";
require "minc_utilities.pl";
require "misc_utilities.pl";
require ParseArgs;
use Startup;
use JobControl;
use MNI::DataDir;

&Startup();

&Initialize();

$tissueMask = "${TmpDir}/tissue_mask.mnc";
if (defined($Class)) {
    # Get binary tissue mask from classified volume
    $minval = $Class - 0.5;
    $maxval = $Class + 0.5;
}
else {
    $minval = 0.5;
    $maxval = 1.5;
}

&Spawn(&AddOptions("$MincMath -short -segment -const2 $minval $maxval $ClassFile $tissueMask", $Verbose, 1));

$NormDomain = "${TmpDir}/norm_domain.mnc";
if ($NormTotalVolume || $NormBlurredMean) {
    &Spawn(&AddOptions("$MincMath -mult ${tissueMask} $Mask $NormDomain", $Verbose, 1));
}
if ($MaskInput) {
    $tissueMask = $NormDomain;
}

# Blur tissue mask
$tissueMaskBlur = "${TmpDir}/tissue_mask";
if (defined($BlurredMask)) {
    $tissueMaskBlur = "${tissueMaskBlur}_blur.mnc";
    &Spawn("cp $BlurredMask $tissueMaskBlur");
}
else {
    &Spawn(&AddOptions("$MincBlur $tissueMask $tissueMaskBlur", $Verbose, 1));
    $tissueMaskBlur = "${tissueMaskBlur}_blur.mnc";
}

# Make sure the blurred mask has no negative values (often left by mincblur)
$temp = "${TmpDir}/temp.mnc";
&Spawn(&AddOptions("$MincMath -const2 0 1e99 -clamp $tissueMaskBlur $temp",
		   $Verbose, 1));
&Spawn("mv $temp $tissueMaskBlur");

$value = 1;
if ($NormTotalVolume) {
    # Normalize w/respect to total tissue volume; use the total brain volume
    # as a reference to keep the values within reasonable bounds
    ($result, $maskVolume) =
	&Spawn("$VolumeStats -quiet -floor 0.5 -sum $Mask");
    chop $maskVolume;
    print "MaskVolume: $maskVolume\n" if $Verbose;
    
    ($result, $value) =
	&Spawn("$VolumeStats -quiet -floor 0.5 -sum $NormDomain");
    chop $value;

    $value /= $maskVolume;
}
elsif ($NormBlurredMean) {
    # Normalize mean intensity over discrete tissue mask
    ($result, $value) = 
	&Spawn("$VolumeStats -quiet -mask $NormDomain -mean $tissueMaskBlur");
    chop $value;
}
print "Value: $value\n" if $Verbose;

unlink($tissueMask) unless ($KeepTmp || ($tissueMask eq $ClassFile));

print "Updating history\n" if $Verbose;
my(@history) = &get_history($ClassFile);
push(@history, $HistoryLine);
&put_history($tissueMaskBlur, @history);

$factor = $TargetValue / $value;

$current = $tissueMaskBlur;

if ($factor != 1) {
    $tissueMaskBlurNorm = "${TmpDir}/tissue_mask_blur_norm.mnc";
    &Spawn(&AddOptions("$MincMath -const $factor -mult $current $tissueMaskBlurNorm",
		       $Verbose, 1));

    unlink($current) unless $KeepTmp;
    $current = $tissueMaskBlurNorm;
}

# If necessary, resample blurred volume to the desired target space
if (defined($OutputSpace)) {
    &Spawn(&AddOptions("$MincResample -like $OutputSpace $current $OutFile",
		       $Verbose, $Clobber));
}
else {
    &Spawn("mv $current $OutFile");
}

&Compress($OutFile);

&Cleanup(1);

# ------------------------------ MNI Header ----------------------------------
#@NAME       : &SetHelp
#@INPUT      : none
#@OUTPUT     : none
#@RETURNS    : nothing
#@DESCRIPTION: Sets the $Help and $Usage globals, and registers them
#              with ParseArgs so that user gets useful error and help
#              messages.
#@METHOD     : 
#@GLOBALS    : $Help, $Usage
#@CALLS      : 
#@CREATED    : 95/08/25, Greg Ward (from code formerly in &ParseArgs)
#@MODIFIED   : 
#-----------------------------------------------------------------------------
sub SetHelp
{
   $Usage = <<USAGE;
Usage: $ProgramName [options] <label.mnc> <out.mnc>

USAGE

   $Help = <<HELP;

$ProgramName 
   produces a smoothed tissue mask, given a label volume.
HELP

   &ParseArgs::SetHelpText ($Help, $Usage);
}

# ------------------------------ MNI Header ----------------------------------
#@NAME       : &Initialize
#@INPUT      : 
#@OUTPUT     : 
#@RETURNS    : 
#@DESCRIPTION: Sets global variables, parses command line, finds required 
#              programs, and sets their options.  Dies on any error.
#@METHOD     : 
#@GLOBALS    : general: $Verbose, $Execute, $Clobber, $KeepTmp
#              mask:    $Mask
#@CALLS      : &SetupArgTables
#              &ParseArgs::Parse
#              
#@CREATED    : 96/01/20, Alex Zijdenbos
#@MODIFIED   : 
#-----------------------------------------------------------------------------
sub Initialize
{
    chop ($ctime = &ctime(time));
    $HistoryLine = "$ctime>>> $0 @ARGV\n";

    $Clobber     = 0;
    $Execute     = 1;
    $Verbose     = 1;
    $Compress    = 0;

    $Class = undef;
    $FWHM  = 14;
    $BlurDimensions = undef;
    $Apodize = '';
    $NormTotalVolume = 0;
    $NormBlurredMean = 0;
    $TargetValue = 1;
    $MaskInput = 0;
    $BlurredMask = undef;
    $OutputSpace = undef;

    $ModelDir    = MNI::DataDir::dir('avg305');
    $Mask        = "average_305_mask_1mm.mnc";
    MNI::DataDir::check_data($ModelDir, [$Mask]);
    $Mask        = "${ModelDir}${Mask}";

    if ($Execute) {
	&CheckOutputDirs($TmpDir);
	if (!$ENV{'TMPDIR'}) {
	    $ENV{'TMPDIR'} = $TmpDir;
	}
    }

    &SetHelp;

    &JobControl::SetOptions("Verbose", $Verbose, 
		     "Execute", $Execute,
 		     "ErrorAction", "fatal",
		     "MergeErrors", 2);
    
    # Locate programs
    ($MincBlur       = &FindProgram ("mincblur"))        || &Fatal();
    ($MincMath       = &FindProgram ("mincmath"))        || &Fatal();
    ($MincResample   = &FindProgram ("mincresample"))    || &Fatal();
    ($ResampleLabels = &FindProgram ("resample_labels")) || &Fatal();
    ($VolumeStats    = &FindProgram ("volume_stats"))    || &Fatal();

    # Setup arg tables and parse args
    ($argsTbl) = &SetupArgTables;

    &ParseArgs::Parse ([@DefaultArgs, @$argsTbl], \@ARGV) || &Fatal();

    # Check source arguments
    my($nArgs) = $#ARGV + 1;
    if ($nArgs != 2) {
	&Fatal("Incorrect number of arguments\n");
    }	
	
    $ClassFile = shift(@ARGV);
    &CheckFiles($ClassFile) || &Fatal();

    $OutFile = shift(@ARGV);
    
    $OutFile =~ s/\.(Z|gz|z)$//;
    if (!$Clobber && -e $OutFile) {
	&Fatal("Output file $OutFile exists; use -clobber to overwrite");
    }

    if ($NormTotalVolume && $NormBlurredMean) {
	&Fatal("Please use only one of -norm_total_volume and -norm_blurred_mean");
    }

    ($Mask = &CheckSampling($Mask, $ClassFile, 1)) || &Fatal();

    $MincBlur .= " -fwhm $FWHM $Apodize";
    $MincBlur .= " -dimensions $BlurDimensions" if defined($BlurDimensions);
}

# ------------------------------ MNI Header ----------------------------------
#@NAME       : &SetupArgTables
#@INPUT      : none
#@OUTPUT     : none
#@RETURNS    : References to the four option tables:
#                @generalArgs
#                @maskArgs
#@DESCRIPTION: Defines the tables of command line (and config file) 
#              options that we pass to ParseArgs.  There are four
#              separate groups of options, because not all of them
#              are valid in all places.  See comments in the routine
#              for details.
#@METHOD     : 
#@GLOBALS    : makes references to many globals (almost all of 'em in fact)
#              even though most of them won't have been defined when
#              this is called
#@CALLS      : 
#@CREATED    : 96/01/29, Alex Zijdenbos
#@MODIFIED   : 
#-----------------------------------------------------------------------------
sub SetupArgTables
{
    my (@args);

    # Preferences -- these may be given in the configuration file
    # or the command line

    @args = 
	(["Mincblur options", "section"],
	 ["-fwhm", "float", 1, \$FWHM,
	  "FWHM of kernel used to blur the tissue volume with [default: $FWHM]"],
	 ["-dimensions", "integer", 1, \$BlurDimensions,
	  "dimensions to blur (1=z, 2=x,y, 3=x,y,z, see mincblur) [default: mincblur default]"],
	 ["-no_apodize", "copy", undef, \$Apodize,
	  "Do not apodize the data before blurring"],

	 ["Other options", "section"],
	 ["-compress|-nocompress", "boolean", 1, \$Compress,
	  "compress the resulting output file(s) [default: -nocompress]"],
	 ["-binvalue", "integer", 1, \$Class,
	  "class index of tissue to process [default: assume a binary file]"],
	 ["-norm_total_volume|-nonorm_total_volume", "boolean", 1, \$NormTotalVolume,
	  "normalize w/respect to the total tissue volume [default: -nonorm_total_volume]"],
	 ["-norm_blurred_mean|-nonorm_blurred_mean", "boolean", 1, \$NormBlurredMean,
	  "normalize w/respect to the mean value of the blurred volume over the binary mask [default: -nonorm_blurred_mean]"],
#	 ["-blurred_mask", "string", 1, \$BlurredMask,
#	  "use this volume as the blurred mask (i.e., only do the normalization)"],
	 ["-mask", "string", 1, \$Mask,
	  "mask to use for normalization [default: $Mask]"],
	 ["-mask_input|-nomask_input", "boolean", 1, \$MaskInput,
	  "mask the label volume prior to processing [default: -nomask_input]"],
	 ["-like", "string", 1, \$OutputSpace,
	  "Model volume for resampling of output volume [default: use input volume sampling]"],
	 ["-target_value", "float", 1, \$TargetValue,
	  "Target value for normalization. If no normalization is requested, the output volume will be multiplied by this factor [default: $TargetValue]"]);
    
    (\@args);
}

# ------------------------------ MNI Header ----------------------------------
#@NAME       : &CheckSampling
#@INPUT      : $file, $model, $isLabelVolume
#@OUTPUT     : none
#@RETURNS    : $file
#@DESCRIPTION: Checks whether the sampling space of $file matches that of $model,
#              and resamples $file if necessary (not allowing for ransformations).
#              when $isLabelVolume is true, resample_labels is used rather than
#              mincresample.
#@METHOD     : 
#@GLOBALS    : Standard ($Execute, ...)
#@CALLS      : 
#@CREATED    : 96/03/19, Alex Zijdenbos
#@MODIFIED   : 
#-----------------------------------------------------------------------------
sub CheckSampling {
    my($file, $model, $isLabelVolume) = @_;

    # Get file params
    my(@fileStart, @fileStep, @fileLength, @fileDircos);
    &volume_params($file, \@fileStart, \@fileStep, \@fileLength, \@fileDircos);

    # Get model params
    my(@modelStart, @modelStep, @modelLength, @modelDircos);
    &volume_params($model, \@modelStart, \@modelStep, \@modelLength, \@modelDircos);
 
    if (!&comp_num_lists(\@fileDircos, \@modelDircos)) {
	print "Direction cosines of $file and $model do not match\n";
	return 0;
    }
    
    # Resample $file if params are different
    if (!&comp_num_lists(\@fileStart, \@modelStart) ||
	!&comp_num_lists(\@fileStep, \@modelStep) ||
	!&comp_num_lists(\@fileLength, \@modelLength)) {
	if ($Verbose) {
	    print "Resampling $file like $model\n";
	}
	my($resampledFile) = &UniqueOutputFile(&ReplaceDir($TmpDir, $file));

	if (defined($isLabelVolume) && $isLabelVolume) {
	    &Spawn(&AddOptions("$ResampleLabels -resample \'-like $model\' $file $resampledFile", $Verbose, 1));
	}
	else {
	    &Spawn(&AddOptions("$MincResample -like $model $file $resampledFile", 
			       $Verbose, 1));
	}
	$file = $resampledFile;
    }

    $file;
}

sub AddOptions {
    my($string, $verbose, $clobber) = @_;

    if ($clobber) {
	$string =~ s/^([^\s]+)(.*)$/$1 -clobber$2/;
    }

    if ($verbose) {
	$string =~ s/^([^\s]+)(.*)$/$1 -verbose$2/;
    }
    else {
	$string =~ s/^([^\s]+)(.*)$/$1 -quiet$2/;
    }

    $string;
}

sub UniqueOutputFile {
    my(@files) = @_;
    my($file);

    foreach $file (@files) {
	$file =~ s/\.(Z|gz|z)$//;
	while (-e $file) {
	    $file .= '_';
	}
    }

    return @files if wantarray;
    return $files[0];
}

sub Compress {
    my($file) = @_;

    if ($Compress) {
	if (!$Clobber && -e "${file}.gz") {
	    print "Compressed file already exists; not compressing $file\n";
	}
	else {
	    &Spawn("gzip -f $file");
	}
    }
}
