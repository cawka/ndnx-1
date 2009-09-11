/**
 * Part of the CCNx Java Library.
 *
 * Copyright (C) 2008, 2009 Palo Alto Research Center, Inc.
 *
 * This library is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License version 2.1
 * as published by the Free Software Foundation. 
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details. You should have received
 * a copy of the GNU Lesser General Public License along with this library;
 * if not, write to the Free Software Foundation, Inc., 51 Franklin Street,
 * Fifth Floor, Boston, MA 02110-1301 USA.
 */

package org.ccnx.ccn.io.net;

import java.io.IOException;
import java.io.InputStream;
import java.net.URL;
import java.net.URLConnection;

import javax.xml.stream.XMLStreamException;

import org.ccnx.ccn.impl.support.Log;
import org.ccnx.ccn.io.CCNInputStream;
import org.ccnx.ccn.protocol.ContentName;
import org.ccnx.ccn.protocol.MalformedContentNameStringException;


public class CCNURLConnection extends URLConnection {

	public CCNURLConnection(URL url) {
		super(url);
	}

	@Override
	public void connect() throws IOException {
	}
	
	@Override
	public InputStream getInputStream() throws IOException {
		ContentName thisName = null;
		try {
			thisName = ContentName.fromURI(this.url.toString());
			return new CCNInputStream(thisName);
		} catch (MalformedContentNameStringException e) {
			Log.info("Cannot parse URI: " + this.url);
			throw new IOException("Cannot parse URI: " + this.url + ": " + e.getMessage());
		} catch (XMLStreamException e) {
			Log.info("Cannot parse XML: " + e.getMessage());
			throw new IOException("Cannot parse XML.: " + e.getMessage());
		}
	}
}
