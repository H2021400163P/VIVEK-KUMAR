
//header files
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/acpi.h>
#include <linux/regmap.h>
#include <linux/iio/iio.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>          
#include <linux/iio/sysfs.h>  /* mandatory since sysfs are used */
#include <linux/iio/events.h> /* For advanced users, to manage iio events */
#include <linux/iio/buffer.h>  /* mandatory to use triggered buffers */

#define pr_fmt(fmt) "bmp280: " fmt

#define BMP280_REG_PRESS_XLSB	0xF9
//[3:0] >> Raw data
#define BMP280_REG_PRESS_LSB	0xF8
//LSB part low[11:4] of the raw pressure measurement output data.
#define BMP280_REG_PRESS_MSB	0xF7
//MSB part up[19:12] of the raw pressure measurement output data. 


//same for temperature
#define BMP280_REG_TEMP_XLSB	0xFC
#define BMP280_REG_TEMP_LSB		0xFB
#define BMP280_REG_TEMP_MSB		0xFA


#define BMP280_REG_CONFIG		0xF5
//controls the rate of filter.IIR

#define BMP280_REG_CTRL_MEAS	0xF4
//Controls oversampling of pressure and temp data and also the last two LSB bits control power modes

#define BMP280_REG_STATUS		0xF3 
//status register contains the status of the device and bit 3 sets to 1 when data is begin converted and 
// written in to the data register 

#define BMP280_REG_RESET		0xE0
//If the value 0xB6 is written to the register, 
//the device is reset using the power on reset process

#define BMP280_REG_ID			0xD0 
//The REGID register contains the chip identification no. 8 bit chip ID, which is 0x58. 
//This no.can be read as soon as the device finished the power-on-reset. 

#define BMP280_REG_COMP_TEMP_START	0x88  // sensor calibration data >> read only
#define BMP280_COMP_TEMP_REG_COUNT	6 
#define BMP280_REG_COMP_PRESS_START	0x8E
#define BMP280_COMP_PRESS_REG_COUNT	18

#define BMP280_FILTER_MASK		(BIT(4) | BIT(3) | BIT(2))
#define BMP280_FILTER_OFF		0
#define BMP280_FILTER_2X		BIT(2)
#define BMP280_FILTER_4X		BIT(3)
#define BMP280_FILTER_8X		(BIT(3) | BIT(2))
#define BMP280_FILTER_16X		BIT(4)

#define BMP280_OSRS_TEMP_MASK	(BIT(7) | BIT(6) | BIT(5))
#define BMP280_OSRS_TEMP_SKIP	0
#define BMP280_OSRS_TEMP_1X		BIT(5)
#define BMP280_OSRS_TEMP_2X		BIT(6)
#define BMP280_OSRS_TEMP_4X		(BIT(6) | BIT(5))
#define BMP280_OSRS_TEMP_8X		BIT(7)
#define BMP280_OSRS_TEMP_16X	(BIT(7) | BIT(5))

#define BMP280_OSRS_PRESS_MASK	(BIT(4) | BIT(3) | BIT(2))
#define BMP280_OSRS_PRESS_SKIP		0
#define BMP280_OSRS_PRESS_1X	BIT(2)
#define BMP280_OSRS_PRESS_2X	BIT(3)
#define BMP280_OSRS_PRESS_4X	(BIT(3) | BIT(2))
#define BMP280_OSRS_PRESS_8X	BIT(4)
#define BMP280_OSRS_PRESS_16X	(BIT(4) | BIT(2))

#define BMP280_MODE_MASK		(BIT(1) | BIT(0))

//Power modes setup
#define BMP280_MODE_SLEEP		0
#define BMP280_MODE_FORCED		BIT(0)
#define BMP280_MODE_NORMAL		(BIT(1) | BIT(0))

#define BMP280_CHIP_ID			0x58 
#define BMP280_SOFT_RESET_VAL	0xB6 //soft reset


struct bmp280_data 
{
	struct i2c_client *client;
	struct mutex lock;
	struct regmap *regmap;
	/*
	 *client represents a slave on I2Cs
	 */
	s32 t_fine; //data 32 bit temperature and presssure
};
/*
 * These enums are used for indexing into the array of compensation
 * parameters.
 */
enum { T1, T2, T3 };
enum { P1, P2, P3, P4, P5, P6, P7, P8, P9 };

/*IIO_chan_spec
BMP280 has twp data channels chaneels are exported to the sysf directory in BUS folder
we can also apply or set the direction of the data flow of the channel, its attributes*/
static const struct iio_chan_spec bmp280_channels[] = {
	{
		.type = IIO_PRESSURE,
		.info_mask_separate = BIT(IIO_CHAN_INFO_PROCESSED),
		//nature of the data coming over the channel processed data

	},
	{
		.type = IIO_TEMP,
		.info_mask_separate = BIT(IIO_CHAN_INFO_PROCESSED), 
		//nature of the data coming over the channel
	},
};

/*declaration of some type of register some are readable type register and some are 
writable type register.. based on the data sheet
*/

static bool bmp280_is_writeable_reg(struct device *dev, unsigned int reg)
{
	switch (reg) 
	{
		/* 
		*/
	case BMP280_REG_CONFIG:
	case BMP280_REG_CTRL_MEAS:
	case BMP280_REG_RESET:
		return true;
	default:
		return false;
	};
}
static bool bmp280_is_volatile_reg(struct device *dev, unsigned int reg)
{
	switch (reg) 
	{
	case BMP280_REG_TEMP_XLSB:
	case BMP280_REG_TEMP_LSB:
	case BMP280_REG_TEMP_MSB:
	case BMP280_REG_PRESS_XLSB:
	case BMP280_REG_PRESS_LSB:
	case BMP280_REG_PRESS_MSB:
	case BMP280_REG_STATUS:
	
	return true;
	default:
	return false;
	}
}
static const struct regmap_config bmp280_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = BMP280_REG_TEMP_XLSB,
	.cache_type = REGCACHE_RBTREE,
	.writeable_reg = bmp280_is_writeable_reg,
	.volatile_reg = bmp280_is_volatile_reg,
};
/*
 * Returns temperature in DegC, resolution is 0.01 DegC.  Output value of
 * "5123" equals 51.23 DegC.
 * t_fine carries fine temperature as global value.
 *
 * Taken from datasheet,compensation formula.
 * calibrations
 */
static s32 bmp280_compensate_temp(struct bmp280_data *data,
				  s32 adc_temp)
{
	int ret;
	s32 var1, var2;
	__le16 buf[BMP280_COMP_TEMP_REG_COUNT / 2];

	ret = regmap_bulk_read(data->regmap, BMP280_REG_COMP_TEMP_START,
			       buf, BMP280_COMP_TEMP_REG_COUNT); 
	// calibrated value captured in ret variable


	if (ret < 0) 
	{
		dev_err(&data->client->dev,
			"failed to read temperature calibration parameters\n");
		return ret;
	}
	/*
	 * Conversely, T1 and P1 are unsigned values, so they can be
	 * cast straight to the larger type.
	 */
	var1 = (((adc_temp >> 3) - ((s32)le16_to_cpu(buf[T1]) << 1)) *
		((s32)(s16)le16_to_cpu(buf[T2]))) >> 11;

	var2 = (((((adc_temp >> 4) - ((s32)le16_to_cpu(buf[T1]))) *
		  ((adc_temp >> 4) - ((s32)le16_to_cpu(buf[T1])))) >> 12) *
		((s32)(s16)le16_to_cpu(buf[T3]))) >> 14;
	data->t_fine = var1 + var2;

	return (data->t_fine * 5 + 128) >> 8;
}
/*
 * Returns pressure in Pa as unsigned 32 bit integer in Q24.8 format (24
 * integer bits and 8 fractional bits).  Output value of "24674867"
 * represents 24674867/256 = 96386.2 Pa = 963.862 hPa
 *
 * Taken from datasheet...Compensation formula.
 */
static u32 bmp280_compensate_press(struct bmp280_data *data,
				   s32 adc_press)
{
	int ret;
	s64 var1, var2, p;
	__le16 buf[BMP280_COMP_PRESS_REG_COUNT / 2];
	ret = regmap_bulk_read(data->regmap, BMP280_REG_COMP_PRESS_START,
			       buf, BMP280_COMP_PRESS_REG_COUNT);

	if (ret < 0) 
	{
		dev_err(&data->client->dev,
			"failed to read pressure calibration parameters\n");
		return ret;
	}
	var1 = ((s64)data->t_fine) - 128000;
	var2 = var1 * var1 * (s64)(s16)le16_to_cpu(buf[P6]);
	var2 += (var1 * (s64)(s16)le16_to_cpu(buf[P5])) << 17;
	var2 += ((s64)(s16)le16_to_cpu(buf[P4])) << 35;
	var1 = ((var1 * var1 * (s64)(s16)le16_to_cpu(buf[P3])) >> 8) +
		((var1 * (s64)(s16)le16_to_cpu(buf[P2])) << 12);

	var1 = ((((s64)1) << 47) + var1) * ((s64)le16_to_cpu(buf[P1])) >> 33;
	if (var1 == 0)
		return 0;


	p = ((((s64)1048576 - adc_press) << 31) - var2) * 3125;
	p = div64_s64(p, var1);
	var1 = (((s64)(s16)le16_to_cpu(buf[P9])) * (p >> 13) * (p >> 13)) >> 25;
	var2 = (((s64)(s16)le16_to_cpu(buf[P8])) * p) >> 19;
	p = ((p + var1 + var2) >> 8) + (((s64)(s16)le16_to_cpu(buf[P7])) << 4);
	return (u32)p;
}

// read the value of raw data and processing it temperature and pressure data
static int bmp280_read_temp(struct bmp280_data *data,int *val)
{
	int ret;
	__be32 tmp = 0;
	s32 adc_temp, comp_temp;
	ret = regmap_bulk_read(data->regmap, BMP280_REG_TEMP_MSB,
			       (u8 *) &tmp, 3);
	if (ret < 0) 
	{
		dev_err(&data->client->dev, "failed to read temperature\n");
		return ret;
	}
	adc_temp = be32_to_cpu(tmp) >> 12;
	comp_temp = bmp280_compensate_temp(data, adc_temp);

	if (val) {
		*val = comp_temp * 10;
		return IIO_VAL_INT;
	}
	return 0;
}
static int bmp280_read_press(struct bmp280_data *data,int *val, int *val2)

{
	int ret;
	__be32 tmp = 0;
	s32 adc_press;
	u32 comp_press;
	/* Read and compensate calibrated temperature so we get a reading of t_fine. */
	ret = bmp280_read_temp(data, NULL);
	if (ret < 0)
		return ret;
	ret = regmap_bulk_read(data->regmap, BMP280_REG_PRESS_MSB,
			       (u8 *) &tmp, 3);
	if (ret < 0) {
		dev_err(&data->client->dev, "failed to read pressure\n");
		return ret;
	}
	adc_press = be32_to_cpu(tmp) >> 12;
	comp_press = bmp280_compensate_press(data, adc_press);
	*val = comp_press;
	*val2 = 256000;
	return IIO_VAL_FRACTIONAL;
}

static int bmp280_read_raw(struct iio_dev *indio_dev,
			   struct iio_chan_spec const *chan,
			   int *val, int *val2, long mask)
{
	int ret;
	struct bmp280_data *data = iio_priv(indio_dev);
	mutex_lock(&data->lock);
	switch (mask) 
	{
	case IIO_CHAN_INFO_PROCESSED:
		switch (chan->type) 
		{
		case IIO_PRESSURE:
			ret = bmp280_read_press(data, val, val2);
			break;
		case IIO_TEMP:
			ret = bmp280_read_temp(data, val);
			break;
		default:
			ret = -EINVAL;
			break;
		}
		break;
	default:
		ret = -EINVAL;
		break;
	}
	mutex_unlock(&data->lock);
	return ret;
} 
static const struct iio_info bmp280_info = {
	.driver_module = THIS_MODULE,
	.read_raw = &bmp280_read_raw,
};

static int bmp280_chip_init(struct bmp280_data *data)
{
	int ret;
	ret = regmap_update_bits(data->regmap, BMP280_REG_CTRL_MEAS,
				 BMP280_OSRS_TEMP_MASK |
				 BMP280_OSRS_PRESS_MASK |
				 BMP280_MODE_MASK,
				 BMP280_OSRS_TEMP_2X |
				 BMP280_OSRS_PRESS_16X |
				 BMP280_MODE_NORMAL);
				  
	if (ret < 0) {
		dev_err(&data->client->dev,
			"failed to write ctrl_meas register\n");
		return ret;
	}
	ret = regmap_update_bits(data->regmap, BMP280_REG_CONFIG,
				 BMP280_FILTER_MASK,
				 BMP280_FILTER_4X);
	if (ret < 0) {
		dev_err(&data->client->dev,
			"failed to write config register\n");
		return ret;
	}
	return ret;
}

/*Initialize IIO device fields with driver specific information (e.g. device name, device channels).*/
/*This function getting called when the slave has been found 
This will be called only once when we load the driver.

..
*/
static int bmp280_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	int ret;
	struct iio_dev *indio_dev;
	struct bmp280_data *data;
	unsigned int chip_id;
	indio_dev = devm_iio_device_alloc(&client->dev, sizeof(*data)); // allocation of memory of IIO device
	if (!indio_dev)
		return -ENOMEM;
	data = iio_priv(indio_dev);
	mutex_init(&data->lock);
	data->client = client;
	indio_dev->dev.parent = &client->dev;

	/*Initialize IIO device fields with driver specific information.
	Initialize IIO device fields with driver specific information (e.g. device name, device channels).*/

	indio_dev->name = id->name;
	indio_dev->channels = bmp280_channels;
	indio_dev->num_channels = ARRAY_SIZE(bmp280_channels);

	indio_dev->info = &bmp280_info;
	indio_dev->modes = INDIO_DIRECT_MODE;
	data->regmap = devm_regmap_init_i2c(client, &bmp280_regmap_config);
	if (IS_ERR(data->regmap)) {
		dev_err(&client->dev, "failed to allocate register map\n");
		return PTR_ERR(data->regmap);
	}


	ret = regmap_read(data->regmap, BMP280_REG_ID, &chip_id);
	if (ret < 0)
		return ret;


	if (chip_id != BMP280_CHIP_ID) 
	{
		dev_err(&client->dev, "bad chip id.  expected %x got %x\n",
			BMP280_CHIP_ID, chip_id);
		return -EINVAL;
	}
	ret = bmp280_chip_init(data);
	if (ret < 0)
		return ret;
	return devm_iio_device_register(&client->dev, indio_dev);
}
static const struct acpi_device_id bmp280_acpi_match[] = {
	{"BMP0280", 0},
	{ },
};
MODULE_DEVICE_TABLE(acpi, bmp280_acpi_match);
static const struct i2c_device_id bmp280_id[] = {
	{"bmp280", 0},
	{ },
};
MODULE_DEVICE_TABLE(i2c, bmp280_id);
static struct i2c_driver bmp280_driver = {
	.driver = {
		.name	= "bmp280",
		.acpi_match_table = ACPI_PTR(bmp280_acpi_match),
	},
	.probe		= bmp280_probe,
	.id_table	= bmp280_id,
};
module_i2c_driver(bmp280_driver);
MODULE_AUTHOR("VIVEK_0163P & ANURAG_0346P");
MODULE_DESCRIPTION("bmp280_PROJECT");
MODULE_LICENSE("GPL");